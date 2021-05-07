//
//  control_surface_integrator.cpp
//  reaper_control_surface_integrator
//
//

#include "control_surface_integrator.h"
#include "control_surface_midi_widgets.h"
#include "control_surface_action_contexts.h"
#include "control_surface_Reaper_actions.h"
#include "control_surface_manager_actions.h"
#include "control_surface_integrator_ui.h"

extern reaper_plugin_info_t *g_reaper_plugin_info;

string GetLineEnding()
{
#ifdef WIN32
    return "\n";
#else
    return "\r\n" ;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct MidiInputPort
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int port_ = 0;
    midi_Input* midiInput_ = nullptr;
    
    MidiInputPort(int port, midi_Input* midiInput) : port_(port), midiInput_(midiInput) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct MidiOutputPort
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int port_ = 0;
    midi_Output* midiOutput_ = nullptr;
    
    MidiOutputPort(int port, midi_Output* midiOutput) : port_(port), midiOutput_(midiOutput) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static map<int, MidiInputPort*> midiInputs_;
static map<int, MidiOutputPort*> midiOutputs_;

static midi_Input* GetMidiInputForPort(int inputPort)
{
    if(midiInputs_.count(inputPort) > 0)
        return midiInputs_[inputPort]->midiInput_; // return existing
    
    // otherwise make new
    midi_Input* newInput = DAW::CreateMIDIInput(inputPort);
    
    if(newInput)
    {
        newInput->start();
        midiInputs_[inputPort] = new MidiInputPort(inputPort, newInput);
        return newInput;
    }
    
    return nullptr;
}

static midi_Output* GetMidiOutputForPort(int outputPort)
{
    if(midiOutputs_.count(outputPort) > 0)
        return midiOutputs_[outputPort]->midiOutput_; // return existing
    
    // otherwise make new
    midi_Output* newOutput = DAW::CreateMIDIOutput(outputPort, false, NULL);
    
    if(newOutput)
    {
        midiOutputs_[outputPort] = new MidiOutputPort(outputPort, newOutput);
        return newOutput;
    }
    
    return nullptr;
}

void ShutdownMidiIO()
{
    for(auto [index, input] : midiInputs_)
        input->midiInput_->stop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static map<string, oscpkt::UdpSocket*> inputSockets_;
static map<string, oscpkt::UdpSocket*> outputSockets_;

static oscpkt::UdpSocket* GetInputSocketForPort(string surfaceName, int inputPort)
{
    if(inputSockets_.count(surfaceName) > 0)
        return inputSockets_[surfaceName]; // return existing
    
    // otherwise make new
    oscpkt::UdpSocket* newInputSocket = new oscpkt::UdpSocket();
    
    if(newInputSocket)
    {
        newInputSocket->bindTo(inputPort);
        
        if (! newInputSocket->isOk())
        {
            //cerr << "Error opening port " << PORT_NUM << ": " << inSocket_.errorMessage() << "\n";
            return nullptr;
        }
        
        inputSockets_[surfaceName] = newInputSocket;
        
        return inputSockets_[surfaceName];
    }
    
    return nullptr;
}

static oscpkt::UdpSocket* GetOutputSocketForAddressAndPort(string surfaceName, string address, int outputPort)
{
    if(outputSockets_.count(surfaceName) > 0)
        return outputSockets_[surfaceName]; // return existing
    
    // otherwise make new
    oscpkt::UdpSocket* newOutputSocket = new oscpkt::UdpSocket();
    
    if(newOutputSocket)
    {
        if( ! newOutputSocket->connectTo(address, outputPort))
        {
            //cerr << "Error connecting " << remoteDeviceIP_ << ": " << outSocket_.errorMessage() << "\n";
            return nullptr;
        }
        
        newOutputSocket->bindTo(outputPort);
        
        if ( ! newOutputSocket->isOk())
        {
            //cerr << "Error opening port " << outPort_ << ": " << outSocket_.errorMessage() << "\n";
            return nullptr;
        }

        outputSockets_[surfaceName] = newOutputSocket;
        
        return outputSockets_[surfaceName];
    }
    
    return nullptr;
}

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
// Parsing
//////////////////////////////////////////////////////////////////////////////////////////////
static void listZoneFiles(const string &path, vector<string> &results)
{
    regex rx(".*\\.zon$");
    
    if (auto dir = opendir(path.c_str())) {
        while (auto f = readdir(dir)) {
            if (!f->d_name || f->d_name[0] == '.') continue;
            if (f->d_type == DT_DIR)
                listZoneFiles(path + f->d_name + "/", results);
            
            if (f->d_type == DT_REG)
                if(regex_match(f->d_name, rx))
                    results.push_back(path + f->d_name);
        }
        closedir(dir);
    }
}

static void GetWidgetNameAndProperties(string line, string &widgetName, string &modifier, bool &isFeedbackInverted, double &holdDelayAmount, bool &isProperty)
{
    istringstream modified_role(line);
    vector<string> modifier_tokens;
    vector<string> modifierSlots = { "", "", "", ""};
    string modifier_token;
    
    while (getline(modified_role, modifier_token, '+'))
        modifier_tokens.push_back(modifier_token);
    
    if(modifier_tokens.size() > 1)
    {
        for(int i = 0; i < modifier_tokens.size() - 1; i++)
        {
            if(modifier_tokens[i] == Shift)
                modifierSlots[0] = Shift + "+";
            else if(modifier_tokens[i] == Option)
                modifierSlots[1] = Option + "+";
            else if(modifier_tokens[i] == Control)
                modifierSlots[2] = Control + "+";
            else if(modifier_tokens[i] == Alt)
                modifierSlots[3] = Alt + "+";

            else if(modifier_tokens[i] == "InvertFB")
                isFeedbackInverted = true;
            else if(modifier_tokens[i] == "Hold")
                holdDelayAmount = 1.0;
            else if(modifier_tokens[i] == "Property")
                isProperty = true;
        }
    }

    widgetName = modifier_tokens[modifier_tokens.size() - 1];
    
    modifier = modifierSlots[0] + modifierSlots[1] + modifierSlots[2] + modifierSlots[3];
}

static void ProcessZoneFile(string filePath, ControlSurface* surface)
{
    vector<string> includedZones;
    bool isInIncludedZonesSection = false;
    
    map<string, map<string, vector<ActionTemplate*>>> widgetActions;
    
    string zoneName = "";
    string zoneAlias = "";
    string navigatorName = "";
    string actionName = "";
    int lineNumber = 0;
    
    ActionTemplate* currentActionTemplate = nullptr;
    
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {           
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            line = line.substr(0, line.find("//")); // remove trailing commewnts
            
            lineNumber++;
            
            // Trim leading and trailing spaces
            line = regex_replace(line, regex("^\\s+|\\s+$"), "", regex_constants::format_default);
            
            if(line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 0)
            {
                if(tokens[0] == "Zone")
                {
                    zoneName = tokens.size() > 1 ? tokens[1] : "";
                    zoneAlias = tokens.size() > 2 ? tokens[2] : "";
                }
                
                else if(tokens[0] == "ZoneEnd" && zoneName != "")
                {
                    currentActionTemplate = nullptr;
                    
                    vector<WidgetActionTemplate*> widgetActionTemplates;

                    for(auto [widgetName, modifierActions] : widgetActions)
                    {
                        WidgetActionTemplate* widgetActionTemplate = new WidgetActionTemplate(widgetName);
                        
                        if(actionName == Shift || actionName == Option || actionName == Control || actionName == Alt)
                            widgetActionTemplate->isModifier = true;

                        for(auto [modifier, actions] : modifierActions)
                        {
                            ActionContextTemplate* actionContextTemplate = new ActionContextTemplate(modifier);
                            
                            for(auto action : actions)
                                actionContextTemplate->members.push_back(action);
                            
                            widgetActionTemplate->actionContextTemplates.push_back(actionContextTemplate);
                        }
                        
                        widgetActionTemplates.push_back(widgetActionTemplate);
                    }
                    
                    vector<Navigator*> navigators;

                    if(navigatorName == "")
                        navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetDefaultNavigator());
                    if(navigatorName == "SelectedTrackNavigator")
                        navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetSelectedTrackNavigator());
                    else if(navigatorName == "FocusedFXNavigator")
                        navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetFocusedFXNavigator());
                    else if(navigatorName == "MasterTrackNavigator")
                        navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetMasterTrackNavigator());
                    else if(navigatorName == "TrackNavigator")
                    {
                        for(int i = 0; i < surface->GetNumChannels(); i++)
                            navigators.push_back(surface->GetNavigatorForChannel(i));
                    }
                    else if(navigatorName == "SendNavigator")
                    {
                        for(int i = 0; i < surface->GetNumSends(); i++)
                            navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetSelectedTrackNavigator());
                    }
                    else if(navigatorName == "FXMenuNavigator")
                    {
                        for(int i = 0; i < surface->GetFXActivationManager()->GetNumFXSlots(); i++)
                            navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetSelectedTrackNavigator());
                    }
                    
                    surface->AddZoneTemplate(new ZoneTemplate(navigators, zoneName, zoneAlias, filePath, includedZones, widgetActionTemplates));
                    
                    includedZones.clear();
                    widgetActions.clear();
                }
                
                else if(tokens[0] == "TrackNavigator" || tokens[0] == "MasterTrackNavigator" || tokens[0] == "SelectedTrackNavigator" || tokens[0] == "FocusedFXNavigator" || tokens[0] == "SendNavigator" || tokens[0] == "FXMenuNavigator")
                    navigatorName = tokens[0];
                
                else if(tokens[0] == "IncludedZones")
                    isInIncludedZonesSection = true;
                
                else if(tokens[0] == "IncludedZonesEnd")
                    isInIncludedZonesSection = false;
                
                else if(tokens.size() == 1 && isInIncludedZonesSection)
                    includedZones.push_back(tokens[0]);
                
                else if(tokens.size() > 1)
                {
                    actionName = tokens[1];
                    
                    string widgetName = "";
                    string modifier = "";
                    bool isFeedbackInverted = false;
                    double holdDelayAmount = 0.0;
                    bool isProperty = false;
                    
                    GetWidgetNameAndProperties(tokens[0], widgetName, modifier, isFeedbackInverted, holdDelayAmount, isProperty);
                    
                    vector<string> params;
                    for(int i = 1; i < tokens.size(); i++)
                        params.push_back(tokens[i]);
                    
                    if(isProperty)
                    {
                        if(currentActionTemplate != nullptr)
                            currentActionTemplate->properties.push_back(params);
                    }
                    else
                    {
                        currentActionTemplate = new ActionTemplate(actionName, params, isFeedbackInverted, holdDelayAmount);
                        widgetActions[widgetName][modifier].push_back(currentActionTemplate);
                    }
                }
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

void SetRGB(vector<string> params, bool &supportsRGB, bool &supportsTrackColor, vector<rgb_color> &RGBValues)
{
    vector<int> rawValues;
    
    auto openCurlyBrace = find(params.begin(), params.end(), "{");
    auto closeCurlyBrace = find(params.begin(), params.end(), "}");
    
    if(openCurlyBrace != params.end() && closeCurlyBrace != params.end())
    {
        for(auto it = openCurlyBrace + 1; it != closeCurlyBrace; ++it)
        {
            string strVal = *(it);
            
            if(strVal == "Track")
            {
                supportsTrackColor = true;
                break;
            }
            else
            {
                if(regex_match(strVal, regex("[0-9]+")))
                {
                    int value = stoi(strVal);
                    value = value < 0 ? 0 : value;
                    value = value > 255 ? 255 : value;
                    
                    rawValues.push_back(value);
                }
            }
        }
        
        if(rawValues.size() % 3 == 0 && rawValues.size() > 2)
        {
            supportsRGB = true;
            
            for(int i = 0; i < rawValues.size(); i += 3)
            {
                rgb_color color;
                
                color.r = rawValues[i];
                color.g = rawValues[i + 1];
                color.b = rawValues[i + 2];
                
                RGBValues.push_back(color);
            }
        }
    }
}

void SetSteppedValues(vector<string> params, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
{
    auto openSquareBrace = find(params.begin(), params.end(), "[");
    auto closeSquareBrace = find(params.begin(), params.end(), "]");
    
    if(openSquareBrace != params.end() && closeSquareBrace != params.end())
    {
        for(auto it = openSquareBrace + 1; it != closeSquareBrace; ++it)
        {
            string strVal = *(it);
            
            if(regex_match(strVal, regex("-?[0-9]+[.][0-9]+")) || regex_match(strVal, regex("-?[0-9]+")))
                steppedValues.push_back(stod(strVal));
            else if(regex_match(strVal, regex("[(]-?[0-9]+[.][0-9]+[)]")))
                deltaValue = stod(strVal.substr( 1, strVal.length() - 2 ));
            else if(regex_match(strVal, regex("[(]-?[0-9]+[)]")))
                acceleratedTickValues.push_back(stod(strVal.substr( 1, strVal.length() - 2 )));
            else if(regex_match(strVal, regex("[(](-?[0-9]+[.][0-9]+[,])+-?[0-9]+[.][0-9]+[)]")))
            {
                istringstream acceleratedDeltaValueStream(strVal.substr( 1, strVal.length() - 2 ));
                string deltaValue;
                
                while (getline(acceleratedDeltaValueStream, deltaValue, ','))
                    acceleratedDeltaValues.push_back(stod(deltaValue));
            }
            else if(regex_match(strVal, regex("[(](-?[0-9]+[,])+-?[0-9]+[)]")))
            {
                istringstream acceleratedTickValueStream(strVal.substr( 1, strVal.length() - 2 ));
                string tickValue;
                
                while (getline(acceleratedTickValueStream, tickValue, ','))
                    acceleratedTickValues.push_back(stod(tickValue));
            }
            else if(regex_match(strVal, regex("-?[0-9]+[.][0-9]+[>]-?[0-9]+[.][0-9]+")) || regex_match(strVal, regex("[0-9]+[-][0-9]+")))
            {
                istringstream range(strVal);
                vector<string> range_tokens;
                string range_token;
                
                while (getline(range, range_token, '>'))
                    range_tokens.push_back(range_token);
                
                if(range_tokens.size() == 2)
                {
                    double firstValue = stod(range_tokens[0]);
                    double lastValue = stod(range_tokens[1]);
                    
                    if(lastValue > firstValue)
                    {
                        rangeMinimum = firstValue;
                        rangeMaximum = lastValue;
                    }
                    else
                    {
                        rangeMinimum = lastValue;
                        rangeMaximum = firstValue;
                    }
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// Widgets
//////////////////////////////////////////////////////////////////////////////
static int strToHex(string valueStr)
{
    return strtol(valueStr.c_str(), nullptr, 16);
}

static void ProcessMidiWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens,  Midi_ControlSurface* surface)
{
    if(tokens.size() < 2)
        return;
    
    string widgetName = tokens[1];

    Widget* widget = new Widget(surface, widgetName);
    
    if(! widget)
        return;
    
    surface->AddWidget(widget);

    vector<vector<string>> tokenLines;
    
    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        line = regex_replace(line, regex(TabChars), " ");
        line = regex_replace(line, regex(CRLFChars), "");

        lineNumber++;
        
        if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        vector<string> tokens(GetTokens(line));
        
        if(tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }
    
    if(tokenLines.size() < 1)
        return;
    
    for(int i = 0; i < tokenLines.size(); i++)
    {
        int size = tokenLines[i].size();
        
        string widgetClass = tokenLines[i][0];

        // Control Signal Generators
        if(widgetClass == "AnyPress" && (size == 4 || size == 7))
            new AnyPress_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        if(widgetClass == "Press" && size == 4)
            new PressRelease_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Press" && size == 7)
            new PressRelease_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        else if(widgetClass == "Fader14Bit" && size == 4)
            new Fader14Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Fader7Bit" && size== 4)
            new Fader7Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Encoder" && size == 4)
            new Encoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Encoder" && size > 4)
            new AcceleratedEncoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), tokenLines[i]);
        else if(widgetClass == "MFTEncoder" && size > 4)
            new MFT_AcceleratedEncoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), tokenLines[i]);
        else if(widgetClass == "EncoderPlain" && size == 4)
            new EncoderPlain_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "EncoderPlainReverse" && size == 4)
            new EncoderPlainReverse_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Touch" && size == 7)
            new Touch_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        else if(widgetClass == "Toggle" && size == 4)
            new Toggle_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));

        // Feedback Processors
        FeedbackProcessor* feedbackProcessor = nullptr;

        if(widgetClass == "FB_TwoState" && size == 7)
        {
            feedbackProcessor = new TwoState_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        }
        else if(widgetClass == "FB_NovationLaunchpadMiniRGB7Bit" && size == 4)
        {
            feedbackProcessor = new NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_MFT_RGB" && size == 4)
        {
            feedbackProcessor = new MFT_RGB_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_FaderportRGB7Bit" && size == 4)
        {
            feedbackProcessor = new FaderportRGB7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(size == 4)
        {
            if(widgetClass == "FB_Fader14Bit")
                feedbackProcessor = new Fader14Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            else if(widgetClass == "FB_Fader7Bit")
                feedbackProcessor = new Fader7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            else if(widgetClass == "FB_Encoder")
                feedbackProcessor = new Encoder_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            else if(widgetClass == "FB_VUMeter")
                feedbackProcessor = new VUMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            else if(widgetClass == "FB_GainReductionMeter")
                feedbackProcessor = new GainReductionMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_MCUTimeDisplay" && size == 1)
        {
            feedbackProcessor = new MCU_TimeDisplay_Midi_FeedbackProcessor(surface, widget);
        }
        else if(widgetClass == "FB_QConProXMasterVUMeter" && size == 2)
        {
            feedbackProcessor = new QConProXMasterVUMeter_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetClass == "FB_MCUVUMeter" || widgetClass == "FB_MCUXTVUMeter") && size == 2)
        {
            int displayType = widgetClass == "FB_MCUVUMeter" ? 0x14 : 0x15;
            
            feedbackProcessor = new MCUVUMeter_Midi_FeedbackProcessor(surface, widget, displayType, stoi(tokenLines[i][1]));
            
            surface->SetHasMCUMeters(displayType);
        }
        else if(widgetClass == "FB_SCE24_Text" && size == 5)
        {
            feedbackProcessor = new SCE24_Text_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]), stoi(tokenLines[i][2]), stoi(tokenLines[i][3]), stoi(tokenLines[i][4]));
        }
        else if((widgetClass == "FB_MCUDisplayUpper" || widgetClass == "FB_MCUDisplayLower" || widgetClass == "FB_MCUXTDisplayUpper" || widgetClass == "FB_MCUXTDisplayLower") && size == 2)
        {
            if(widgetClass == "FB_MCUDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUXTDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUXTDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
        }
        
        else if((widgetClass == "FB_C4DisplayUpper" || widgetClass == "FB_C4DisplayLower") && size == 3)
        {
            if(widgetClass == "FB_C4DisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
            else if(widgetClass == "FB_C4DisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
        }
        
        else if((widgetClass == "FB_FP8Display" || widgetClass == "FB_FP16Display"
                 || widgetClass == "FB_FP8DisplayUpper" || widgetClass == "FB_FP16DisplayUpper"
                 || widgetClass == "FB_FP8DisplayUpperMiddle" || widgetClass == "FB_FP16DisplayUpperMiddle"
                 || widgetClass == "FB_FP8DisplayLowerMiddle" || widgetClass == "FB_FP16DisplayLowerMiddle"
                 || widgetClass == "FB_FP8DisplayLower" || widgetClass == "FB_FP16DisplayLower") && size == 2)
        {
            if(widgetClass == "FB_FP8Display" || widgetClass == "FB_FP8DisplayUpper")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x00);
            else if(widgetClass == "FB_FP8DisplayUpperMiddle")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x01);
            else if(widgetClass == "FB_FP8DisplayLowerMiddle")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x02);
            else if(widgetClass == "FB_FP8DisplayLower")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x03);

            else if(widgetClass == "FB_FP16Display" ||  widgetClass == "FB_FP16DisplayUpper")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x00);
            else if(widgetClass == "FB_FP16DisplayUpperMiddle")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x01);
            else if(widgetClass == "FB_FP16DisplayLowerMiddle")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x02);
            else if(widgetClass == "FB_FP16DisplayLower")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x03);
        }
        
        else if((widgetClass == "FB_QConLiteDisplayUpper" || widgetClass == "FB_QConLiteDisplayUpperMid" || widgetClass == "FB_QConLiteDisplayLowerMid" || widgetClass == "FB_QConLiteDisplayLower") && size == 2)
        {
            if(widgetClass == "FB_QConLiteDisplayUpper")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_QConLiteDisplayUpperMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_QConLiteDisplayLowerMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 2, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_QConLiteDisplayLower")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 3, 0x14, 0x12, stoi(tokenLines[i][1]));
        }

        
        if(feedbackProcessor != nullptr)
            widget->AddFeedbackProcessor(feedbackProcessor);
    }
}

static void ProcessOSCWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens,  OSC_ControlSurface* surface)
{
    if(tokens.size() < 2)
        return;
    
    Widget* widget = new Widget(surface, tokens[1]);
    
    if(! widget)
        return;
    
    surface->AddWidget(widget);

    vector<vector<string>> tokenLines;

    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        line = regex_replace(line, regex(TabChars), " ");
        line = regex_replace(line, regex(CRLFChars), "");

        lineNumber++;
        
        if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        vector<string> tokens(GetTokens(line));
        
        if(tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }

    for(auto tokenLine : tokenLines)
    {
        if(tokenLine.size() > 1 && tokenLine[0] == "Control")
            new CSIMessageGenerator(surface, widget, tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "Touch")
            new Touch_CSIMessageGenerator(surface, widget, tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "FB_Processor")
            widget->AddFeedbackProcessor(new OSC_FeedbackProcessor(surface, widget, tokenLine[1]));
    }
}

static void ProcessWidgetFile(string filePath, ControlSurface* surface)
{
    int lineNumber = 0;
    
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            lineNumber++;
            
            if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 0 && tokens[0] == "Widget")
            {
                if(filePath[filePath.length() - 3] == 'm')
                    ProcessMidiWidget(lineNumber, file, tokens, (Midi_ControlSurface*)surface);
                if(filePath[filePath.length() - 3] == 'o')
                    ProcessOSCWidget(lineNumber, file, tokens, (OSC_ControlSurface*)surface);
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Manager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Manager::InitActionsDictionary()
{    
    actions_["TrackAutoMode"] =                     new TrackAutoMode();
    actions_["GlobalAutoMode"] =                    new GlobalAutoMode();
    actions_["TrackAutoModeDisplay"] =              new TrackAutoModeDisplay();
    actions_["TimeDisplay"] =                       new TimeDisplay();
    actions_["EuConTimeDisplay"] =                  new EuConTimeDisplay();
    actions_["NoAction"] =                          new NoAction();
    actions_["Reaper"] =                            new ReaperAction();
    actions_["FixedTextDisplay"] =                  new FixedTextDisplay(); ;
    actions_["FixedRGBColourDisplay"] =             new FixedRGBColourDisplay();
    actions_["Rewind"] =                            new Rewind();
    actions_["FastForward"] =                       new FastForward();
    actions_["Play"] =                              new Play();
    actions_["Stop"] =                              new Stop();
    actions_["Record"] =                            new Record();
    actions_["CycleTimeline"] =                     new CycleTimeline();
    actions_["ToggleScrollLink"] =                  new ToggleScrollLink();
    actions_["ForceScrollLink"] =                   new ForceScrollLink();
    actions_["ToggleVCAMode"] =                     new ToggleVCAMode();
    actions_["CycleTimeDisplayModes"] =             new CycleTimeDisplayModes();
    actions_["NextPage"] =                          new GoNextPage();
    actions_["GoPage"] =                            new GoPage();
    actions_["PageNameDisplay"] =                   new PageNameDisplay();
    actions_["GoZone"] =                            new GoZone();
    actions_["TrackBank"] =                         new TrackBank();
    actions_["MappedWidgetsBank"] =                 new MappedWidgetsBank();
    actions_["ClearAllSolo"] =                      new ClearAllSolo();
    actions_["Shift"] =                             new SetShift();
    actions_["Option"] =                            new SetOption();
    actions_["Control"] =                           new SetControl();
    actions_["Alt"] =                               new SetAlt();
    actions_["MapSelectedTrackSendsToWidgets"] =    new MapSelectedTrackSendsToWidgets();
    actions_["MapSelectedTrackFXToWidgets"] =       new MapSelectedTrackFXToWidgets();
    actions_["MapSelectedTrackFXToMenu"] =          new MapSelectedTrackFXToMenu();
    actions_["MapFocusedFXToWidgets"] =             new MapFocusedFXToWidgets();
    actions_["GoFXSlot"] =                          new GoFXSlot();
    actions_["CycleTrackAutoMode"] =                new CycleTrackAutoMode();
    actions_["FocusedFXParam"] =                    new FocusedFXParam();
    actions_["FocusedFXParamNameDisplay"] =         new FocusedFXParamNameDisplay();
    actions_["FocusedFXParamValueDisplay"] =        new FocusedFXParamValueDisplay();
    actions_["TrackVolume"] =                       new TrackVolume();
    actions_["SoftTakeover7BitTrackVolume"] =       new SoftTakeover7BitTrackVolume();
    actions_["SoftTakeover14BitTrackVolume"] =      new SoftTakeover14BitTrackVolume();
    actions_["TrackVolumeDB"] =                     new TrackVolumeDB();
    actions_["TrackToggleVCASpill"] =               new TrackToggleVCASpill();
    actions_["TrackSelect"] =                       new TrackSelect();
    actions_["TrackUniqueSelect"] =                 new TrackUniqueSelect();
    actions_["TrackRangeSelect"] =                  new TrackRangeSelect();
    actions_["TrackRecordArm"] =                    new TrackRecordArm();
    actions_["TrackMute"] =                         new TrackMute();
    actions_["TrackSolo"] =                         new TrackSolo();
    actions_["MCUTrackPan"] =                       new MCUTrackPan();
    actions_["TrackPan"] =                          new TrackPan();
    actions_["TrackPanPercent"] =                   new TrackPanPercent();
    actions_["TrackPanWidth"] =                     new TrackPanWidth();
    actions_["TrackPanWidthPercent"] =              new TrackPanWidthPercent();
    actions_["TrackPanL"] =                         new TrackPanL();
    actions_["TrackPanLPercent"] =                  new TrackPanLPercent();
    actions_["TrackPanR"] =                         new TrackPanR();
    actions_["TrackPanRPercent"] =                  new TrackPanRPercent();
    actions_["TogglePin"] =                         new TogglePin();
    actions_["TrackNameDisplay"] =                  new TrackNameDisplay();
    actions_["TrackVolumeDisplay"] =                new TrackVolumeDisplay();
    actions_["MCUTrackPanDisplay"] =                new MCUTrackPanDisplay();
    actions_["TrackPanDisplay"] =                   new TrackPanDisplay();
    actions_["TrackPanWidthDisplay"] =              new TrackPanWidthDisplay();
    actions_["TrackPanLeftDisplay"] =               new TrackPanLeftDisplay();
    actions_["TrackPanRightDisplay"] =              new TrackPanRightDisplay();
    actions_["TrackOutputMeter"] =                  new TrackOutputMeter();
    actions_["TrackOutputMeterAverageLR"] =         new TrackOutputMeterAverageLR();
    actions_["TrackOutputMeterMaxPeakLR"] =         new TrackOutputMeterMaxPeakLR();
    actions_["FXParam"] =                           new FXParam();
    actions_["FXParamRelative"] =                   new FXParamRelative();
    actions_["FXNameDisplay"] =                     new FXNameDisplay();
    actions_["FXMenuNameDisplay"] =                 new FXMenuNameDisplay();
    actions_["FXParamNameDisplay"] =                new FXParamNameDisplay();
    actions_["FXParamValueDisplay"] =               new FXParamValueDisplay();
    actions_["FXGainReductionMeter"] =              new FXGainReductionMeter();
    actions_["TrackSendVolume"] =                   new TrackSendVolume();
    actions_["TrackSendVolumeDB"] =                 new TrackSendVolumeDB();
    actions_["TrackSendPan"] =                      new TrackSendPan();
    actions_["TrackSendPanPercent"] =               new TrackSendPanPercent();
    actions_["TrackSendMute"] =                     new TrackSendMute();
    actions_["EuConTrackSendMute"] =                new EuConTrackSendMute();
    actions_["TrackSendInvertPolarity"] =           new TrackSendInvertPolarity();
    actions_["EuConTrackSendInvertPolarity"] =      new EuConTrackSendInvertPolarity();
    actions_["TrackSendPrePost"] =                  new TrackSendPrePost();
    actions_["EuConTrackSendPrePost"] =             new EuConTrackSendPrePost();
    actions_["TrackSendNameDisplay"] =              new TrackSendNameDisplay();
    actions_["EuConTrackSendNameDisplay"] =         new EuConTrackSendNameDisplay();
    actions_["TrackSendVolumeDisplay"] =            new TrackSendVolumeDisplay();
    actions_["EuConTrackSendVolumeDisplay"] =       new EuConTrackSendVolumeDisplay();
    actions_["TrackSendPanDisplay"] =               new TrackSendPanDisplay();
    actions_["EuConTrackSendPanDisplay"] =          new EuConTrackSendPanDisplay();
    actions_["TrackSendPrePostDisplay"] =           new TrackSendPrePostDisplay();
    actions_["EuConTrackSendPrePostDisplay"] =      new EuConTrackSendPrePostDisplay();
}

void Manager::Init()
{
    pages_.clear();

    Page* currentPage = nullptr;
    
    string iniFilePath = string(DAW::GetResourcePath()) + "/CSI/CSI.ini";
    
    int lineNumber = 0;
    
    try
    {
        ifstream iniFile(iniFilePath);
        
        int numChannels = 0;
        int numSends = 0;
        int numFX = 0;

        for (string line; getline(iniFile, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 4) // ignore comment lines and blank lines
            {
                if(tokens[0] == MidiSurfaceToken && tokens.size() == 10)
                {
                    if(atoi(tokens[6].c_str()) + atoi(tokens[9].c_str()) > numChannels )
                        numChannels = atoi(tokens[6].c_str()) + atoi(tokens[9].c_str());
                    if (atoi(tokens[7].c_str()) > numSends)
                        numSends = atoi(tokens[7].c_str());
                    if (atoi(tokens[8].c_str()) > numFX)
                        numFX = atoi(tokens[8].c_str());

                }
                else if(tokens[0] == OSCSurfaceToken && tokens.size() == 11)
                {
                    if(atoi(tokens[6].c_str()) + atoi(tokens[9].c_str()) > numChannels )
                        numChannels = atoi(tokens[6].c_str()) + atoi(tokens[9].c_str());
                    if (atoi(tokens[7].c_str()) > numSends)
                        numSends = atoi(tokens[7].c_str());
                    if (atoi(tokens[8].c_str()) > numFX)
                        numFX = atoi(tokens[8].c_str());
                }
                else if(tokens[0] == EuConSurfaceToken && tokens.size() == 7)
                {
                    if(atoi(tokens[3].c_str()) + atoi(tokens[6].c_str()) > numChannels )
                        numChannels = atoi(tokens[3].c_str()) + atoi(tokens[6].c_str());
                    if (atoi(tokens[4].c_str()) > numSends)
                        numSends = atoi(tokens[4].c_str());
                    if (atoi(tokens[5].c_str()) > numFX)
                        numFX = atoi(tokens[5].c_str());
                }
            }
        }
        
        iniFile.clear();
        iniFile.seekg(0);
        
        for (string line; getline(iniFile, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 4) // ignore comment lines and blank lines
            {
                if(tokens[0] == PageToken)
                {
                    if( ! (tokens.size() == 11  || tokens.size() == 5))
                        continue;

                    currentPage = new Page(tokens[1], tokens[2] == "FollowMCP" ? true : false, tokens[3] == "SynchPages" ? true : false, tokens[4] == "UseScrollLink" ? true : false, numChannels, numSends, numFX);
                    pages_.push_back(currentPage);
                }
                else if(tokens[0] == MidiSurfaceToken || tokens[0] == OSCSurfaceToken || tokens[0] == EuConSurfaceToken)
                {
                    int inPort = 0;
                    int outPort = 0;
                    
                    if(tokens[0] == MidiSurfaceToken || tokens[0] == OSCSurfaceToken)
                    {
                        inPort = atoi(tokens[2].c_str());
                        outPort = atoi(tokens[3].c_str());
                    }
                    
                    if(currentPage)
                    {
                        ControlSurface* surface = nullptr;
                        
                        if(tokens[0] == MidiSurfaceToken && tokens.size() == 10)
                            surface = new Midi_ControlSurface(CSurfIntegrator_, currentPage, tokens[1], tokens[4], tokens[5], atoi(tokens[6].c_str()), atoi(tokens[7].c_str()), atoi(tokens[8].c_str()), atoi(tokens[9].c_str()), GetMidiInputForPort(inPort), GetMidiOutputForPort(outPort));
                        else if(tokens[0] == OSCSurfaceToken && tokens.size() == 11)
                            surface = new OSC_ControlSurface(CSurfIntegrator_, currentPage, tokens[1], tokens[4], tokens[5], atoi(tokens[6].c_str()), atoi(tokens[7].c_str()), atoi(tokens[8].c_str()), atoi(tokens[9].c_str()), GetInputSocketForPort(tokens[1], inPort), GetOutputSocketForAddressAndPort(tokens[1], tokens[10], outPort));
                        else if(tokens[0] == EuConSurfaceToken && tokens.size() == 7)
                            surface = new EuCon_ControlSurface(CSurfIntegrator_, currentPage, tokens[1], tokens[2], atoi(tokens[3].c_str()), atoi(tokens[4].c_str()), atoi(tokens[5].c_str()), atoi(tokens[6].c_str()));

                        currentPage->AddSurface(surface);
                    }
                }
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", iniFilePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
    
    if(pages_.size() > 0)
        pages_[currentPageIndex_]->OnInitialization();
}
//////////////////////////////////////////////////////////////////////////////////////////////
// Parsing end
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////
// TrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
void TrackNavigator::PinChannel()
{
    if( ! isChannelPinned_)
    {
        pinnedTrack_ = GetTrack();
        
        isChannelPinned_ = true;
        
        manager_->IncChannelBias(pinnedTrack_, channelNum_);
    }
}

void TrackNavigator::UnpinChannel()
{
    if(isChannelPinned_)
    {
        manager_->DecChannelBias(pinnedTrack_, channelNum_);
        
        isChannelPinned_ = false;
        
        pinnedTrack_ = nullptr;
    }
}

MediaTrack* TrackNavigator::GetTrack()
{
    if(isChannelPinned_)
        return pinnedTrack_;
    else
        return manager_->GetTrackFromChannel(channelNum_ - bias_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// MasterTrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* MasterTrackNavigator::GetTrack()
{
    return DAW::GetMasterTrack(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// SelectedTrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* SelectedTrackNavigator::GetTrack()
{
    return page_->GetTrackNavigationManager()->GetSelectedTrack();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// FocusedFXNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* FocusedFXNavigator::GetTrack()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int fxIndex = 0;
    
    if(DAW::GetFocusedFX(&trackNumber, &itemNumber, &fxIndex) == 1) // Track FX
        return page_->GetTrackNavigationManager()->GetTrackFromId(trackNumber);
    else
        return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MappedWsigetsNavigationManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void MappedWidgetsNavigationManager::AdjustMappedWidgetsBank(int amount)
{
    MediaTrack* track = page_->GetTrackNavigationManager()->GetSelectedTrack();
    if (track == nullptr) 
        return;

    int trackNumSends = GetTrackNumSends(track, 0);
    int trackNumFX = TrackFX_GetCount(track);

    if (trackNumSends <= sendsMappingNum_)
        mappedSendsOffset_ = 0;
    else
    {
        mappedSendsOffset_ += amount;

        if (mappedSendsOffset_ < 0)
            mappedSendsOffset_ = 0;

        int sendsTop = trackNumSends - sendsMappingNum_;

        if (mappedSendsOffset_ > sendsTop)
            mappedSendsOffset_ = sendsTop;
    }

    if (trackNumFX <= fxMenuMappingNum_)
        mappedFxMenuOffset_ = 0;
    else
    {
        mappedFxMenuOffset_ += amount;

        if (mappedFxMenuOffset_ < 0)
            mappedFxMenuOffset_ = 0;

        int fxTop = trackNumFX - fxMenuMappingNum_;

        if (mappedFxMenuOffset_ > fxTop)
            mappedFxMenuOffset_ = fxTop;
    }
}

void MappedWidgetsNavigationManager::ResetMappedWidgetsBankIfTrackChanged()
{
    MediaTrack* track = page_->GetTrackNavigationManager()->GetSelectedTrack();
    if (track == nullptr) 
    {
        previousSelectedTrack = nullptr;
        ResetMappedWidgetsBank();
    }
    else if (track != previousSelectedTrack)
    {
        previousSelectedTrack = track;
        ResetMappedWidgetsBank();
    }
}

void MappedWidgetsNavigationManager::ResetMappedWidgetsBank()
{
    mappedSendsOffset_ = 0;
    mappedFxMenuOffset_ = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// TrackNavigationManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void TrackNavigationManager::ForceScrollLink()
{
    // Make sure selected track is visble on the control surface
    MediaTrack* selectedTrack = GetSelectedTrack();
    
    if(selectedTrack != nullptr)
    {
        for(auto navigator : navigators_)
            if(selectedTrack == navigator->GetTrack())
                return;
        
        for(int i = 0; i < tracks_.size(); i++)
            if(selectedTrack == tracks_[i])
                trackOffset_ = i;
        
        trackOffset_ -= targetScrollLinkChannel_;
        
        if(trackOffset_ <  0)
            trackOffset_ =  0;
        
        int top = GetNumTracks() - navigators_.size();
        
        if(trackOffset_ >  top)
            trackOffset_ = top;
    }
}

void TrackNavigationManager::OnTrackSelectionBySurface(MediaTrack* track)
{
    if(scrollLink_)
    {
        if(DAW::IsTrackVisible(track, true))
            DAW::SetMixerScroll(track); // scroll selected MCP tracks into view
        
        if(DAW::IsTrackVisible(track, false))
            DAW::SendCommandMessage(40913); // scroll selected TCP tracks into view
    }
}

void TrackNavigationManager::AdjustTrackBank(int amount)
{
    int numTracks = GetNumTracks();
    
    if(numTracks <= navigators_.size())
        return;
   
    trackOffset_ += amount;
    
    if(trackOffset_ <  0)
        trackOffset_ =  0;
    
    int top = numTracks - navigators_.size();
    
    if(trackOffset_ >  top)
        trackOffset_ = top;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ActionContext::ActionContext(Action* action, Widget* widget, Zone* zone, vector<string> params, vector<vector<string>> properties): action_(action), widget_(widget), zone_(zone), properties_(properties)
{
    widget->SetProperties(properties);
    
    string actionName = "";
    
    if(params.size() > 0)
        actionName = params[0];
    
    // Action with int param, could include leading minus sign
    if(params.size() > 1 && (isdigit(params[1][0]) ||  params[1][0] == '-'))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        intParam_= atol(params[1].c_str());
    }
    
    // Action with param index, must be positive
    if(params.size() > 1 && isdigit(params[1][0]))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    // Action with string param
    if(params.size() > 1)
        stringParam_ = params[1];
    
    if(actionName == "TrackVolumeDB" || actionName == "TrackSendVolumeDB")
    {
        rangeMinimum_ = -144.0;
        rangeMaximum_ = 24.0;
    }
    
    if(actionName == "TrackPanPercent" || actionName == "TrackPanWidthPercent" || actionName == "TrackPanLPercent" || actionName == "TrackPanRPercent")
    {
        rangeMinimum_ = -100.0;
        rangeMaximum_ = 100.0;
    }
   
    if(actionName == "Reaper" && params.size() > 1)
    {
        if (isdigit(params[1][0]))
        {
            commandId_ =  atol(params[1].c_str());
        }
        else // look up by string
        {
            commandId_ = DAW::NamedCommandLookup(params[1].c_str());
            
            if(commandId_ == 0) // can't find it
                commandId_ = 65535; // no-op
        }
    }
    
    if(actionName == "FXParam" && params.size() > 1 && isdigit(params[1][0])) // C++ 11 says empty strings can be queried without catastrophe :)
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    if(actionName == "FXParamValueDisplay" && params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
        
        if(params.size() > 2 && params[2] != "[" && params[2] != "{" && isdigit(params[2][0]))
        {
            shouldUseDisplayStyle_ = true;
            displayStyle_ = atol(params[2].c_str());
        }
    }
    
    if(actionName == "FXParamNameDisplay" && params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
        
        if(params.size() > 2 && params[2] != "{" && params[2] != "[")
            fxParamDisplayName_ = params[2];
    }
    
    if(actionName == "MCUTrackPanDisplay"&& params.size() > 1)
    {
        SetAssociatedWidget(GetSurface()->GetWidgetByName(params[1]));
    }
    
    if(params.size() > 0)
    {
        SetRGB(params, supportsRGB_, supportsTrackColor_, RGBValues_);
        SetSteppedValues(params, deltaValue_, acceleratedDeltaValues_, rangeMinimum_, rangeMaximum_, steppedValues_, acceleratedTickValues_);
    }
    
    if(acceleratedTickValues_.size() < 1)
        acceleratedTickValues_.push_back(10);

}

Page* ActionContext::GetPage()
{
    return widget_->GetSurface()->GetPage();
}

ControlSurface* ActionContext::GetSurface()
{
    return widget_->GetSurface();
}

TrackNavigationManager* ActionContext::GetTrackNavigationManager()
{
    return GetPage()->GetTrackNavigationManager();
}

MappedWidgetsNavigationManager* ActionContext::GetMappedWidgetsNavigationManager()
{
    return GetPage()->GetMappedWidgetsNavigationManager();
}

MediaTrack* ActionContext::GetTrack()
{
    return zone_->GetNavigator()->GetTrack();
}

int ActionContext::GetSlotIndex()
{
    return zone_->GetSlotIndex();
}

string ActionContext::GetName()
{
    return zone_->GetName();
}

void ActionContext::RunDeferredActions()
{
    if(holdDelayAmount_ != 0.0 && delayStartTime_ != 0.0 && DAW::GetCurrentNumberOfMilliseconds() > (delayStartTime_ + holdDelayAmount_))
    {
        DoRangeBoundAction(deferredValue_);
        
        delayStartTime_ = 0.0;
        deferredValue_ = 0.0;
    }
}

void ActionContext::RequestUpdate()
{   
    action_->RequestUpdate(this);
}

void ActionContext::ClearWidget()
{
    widget_->Clear();
}

void ActionContext::UpdateWidgetValue(double value)
{
    if(steppedValues_.size() > 0)
        SetSteppedValueIndex(value);

    value = isFeedbackInverted_ == false ? value : 1.0 - value;
   
    widget_->UpdateValue(value);

    if(supportsRGB_)
    {
        currentRGBIndex_ = value == 0 ? 0 : 1;
        widget_->UpdateRGBValue(RGBValues_[currentRGBIndex_].r, RGBValues_[currentRGBIndex_].g, RGBValues_[currentRGBIndex_].b);
    }
    else if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->UpdateRGBValue(r, g, b);
        }
    }
}

void ActionContext::UpdateWidgetValue(int param, double value)
{
    if(steppedValues_.size() > 0)
        SetSteppedValueIndex(value);

    value = isFeedbackInverted_ == false ? value : 1.0 - value;
        
    widget_->UpdateValue(param, value);
    
    currentRGBIndex_ = value == 0 ? 0 : 1;
    
    if(supportsRGB_)
    {
        currentRGBIndex_ = value == 0 ? 0 : 1;
        widget_->UpdateRGBValue(RGBValues_[currentRGBIndex_].r, RGBValues_[currentRGBIndex_].g, RGBValues_[currentRGBIndex_].b);
    }
    else if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->UpdateRGBValue(r, g, b);
        }
    }
}

void ActionContext::UpdateWidgetValue(string value)
{
    if(supportsRGB_ && RGBValues_.size() == 2 )
        widget_->UpdateValueAndColors(value, RGBValues_[0], RGBValues_[1]);
    else
        widget_->UpdateValue(value);
    /*
    if(supportsRGB_)
    {
        currentRGBIndex_ = 1;
        widget_->UpdateRGBValue(RGBValues_[currentRGBIndex_].r, RGBValues_[currentRGBIndex_].g, RGBValues_[currentRGBIndex_].b);
    }
    else if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->UpdateRGBValue(r, g, b);
        }
    }
     */
}

void ActionContext::ForceWidgetValue(double value)
{
    if(steppedValues_.size() > 0)
        SetSteppedValueIndex(value);
    
    value = isFeedbackInverted_ == false ? value : 1.0 - value;
    
    widget_->ForceValue(value);

    if(supportsRGB_)
    {
        currentRGBIndex_ = value == 0 ? 0 : 1;
        widget_->ForceRGBValue(RGBValues_[currentRGBIndex_].r, RGBValues_[currentRGBIndex_].g, RGBValues_[currentRGBIndex_].b);
    }
    else if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->ForceRGBValue(r, g, b);
        }
    }
}

void ActionContext::DoAction(double value)
{
    if(holdDelayAmount_ != 0.0)
    {
        if(value == 0.0)
        {
            deferredValue_ = 0.0;
            delayStartTime_ = 0.0;
        }
        else
        {
            deferredValue_ = value;
            delayStartTime_ =  DAW::GetCurrentNumberOfMilliseconds();
        }
    }
    else
    {
        if(steppedValues_.size() > 0)
        {
            if(value != 0.0) // ignore release messages
            {
                if(steppedValuesIndex_ == steppedValues_.size() - 1)
                {
                    if(steppedValues_[0] < steppedValues_[steppedValuesIndex_]) // GAW -- only wrap if 1st value is lower
                        steppedValuesIndex_ = 0;
                }
                else
                    steppedValuesIndex_++;
                
                DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
            }
        }
        else
            DoRangeBoundAction(value);
    }
}

void ActionContext::DoRelativeAction(double delta)
{
    if(steppedValues_.size() > 0)
        DoSteppedValueAction(delta);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + delta);
}

void ActionContext::DoRelativeAction(int accelerationIndex, double delta)
{
    if(steppedValues_.size() > 0)
        DoAcceleratedSteppedValueAction(accelerationIndex, delta);
    else if(acceleratedDeltaValues_.size() > 0)
        DoAcceleratedDeltaValueAction(accelerationIndex, delta);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + delta);
}

void ActionContext::DoRangeBoundAction(double value)
{
    if(value > rangeMaximum_)
        value = rangeMaximum_;
    
    if(value < rangeMinimum_)
        value = rangeMinimum_;
    
    action_->Do(this, value);
}

void ActionContext::DoSteppedValueAction(double delta)
{
    if(delta > 0)
    {
        steppedValuesIndex_++;
        
        if(steppedValuesIndex_ > steppedValues_.size() - 1)
            steppedValuesIndex_ = steppedValues_.size() - 1;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
    else
    {
        steppedValuesIndex_--;
        
        if(steppedValuesIndex_ < 0 )
            steppedValuesIndex_ = 0;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
}

void ActionContext::DoAcceleratedSteppedValueAction(int accelerationIndex, double delta)
{
    if(delta > 0)
    {
        accumulatedIncTicks_++;
        accumulatedDecTicks_ = accumulatedDecTicks_ - 1 < 0 ? 0 : accumulatedDecTicks_ - 1;
    }
    else if(delta < 0)
    {
        accumulatedDecTicks_++;
        accumulatedIncTicks_ = accumulatedIncTicks_ - 1 < 0 ? 0 : accumulatedIncTicks_ - 1;
    }
    
    accelerationIndex = accelerationIndex > acceleratedTickValues_.size() - 1 ? acceleratedTickValues_.size() - 1 : accelerationIndex;
    accelerationIndex = accelerationIndex < 0 ? 0 : accelerationIndex;
    
    if(delta > 0 && accumulatedIncTicks_ >= acceleratedTickValues_[accelerationIndex])
    {
        accumulatedIncTicks_ = 0;
        accumulatedDecTicks_ = 0;
        
        steppedValuesIndex_++;
        
        if(steppedValuesIndex_ > steppedValues_.size() - 1)
            steppedValuesIndex_ = steppedValues_.size() - 1;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
    else if(delta < 0 && accumulatedDecTicks_ >= acceleratedTickValues_[accelerationIndex])
    {
        accumulatedIncTicks_ = 0;
        accumulatedDecTicks_ = 0;
        
        steppedValuesIndex_--;
        
        if(steppedValuesIndex_ < 0 )
            steppedValuesIndex_ = 0;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
}

void ActionContext::DoAcceleratedDeltaValueAction(int accelerationIndex, double delta)
{
    accelerationIndex = accelerationIndex > acceleratedDeltaValues_.size() - 1 ? acceleratedDeltaValues_.size() - 1 : accelerationIndex;
    accelerationIndex = accelerationIndex < 0 ? 0 : accelerationIndex;
    
    if(delta > 0.0)
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + acceleratedDeltaValues_[accelerationIndex]);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) - acceleratedDeltaValues_[accelerationIndex]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// WidgetActionBroker
////////////////////////////////////////////////////////////////////////////////////////////////////////
vector<ActionContext>& WidgetContext::GetActionContexts()
{
    string modifier = "";
    
    if( ! widget_->GetIsModifier())
        modifier = widget_->GetSurface()->GetPage()->GetModifier();
    
    if(actionContextDictionary_.count(modifier) > 0)
        return actionContextDictionary_[modifier];
    else if(actionContextDictionary_.count("") > 0)
        return actionContextDictionary_[""];
    else
        return defaultContexts_;
}

void WidgetContext::GetFormattedFXParamValue(char *buffer, int bufferSize)
{
    if(zone_->GetNavigator()->GetTrack() != nullptr)
    {
        int paramIndex = GetActionContexts().size() > 0 ? GetActionContexts()[0].GetParamIndex() : 0;
        
        DAW::TrackFX_GetFormattedParamValue(zone_->GetNavigator()->GetTrack(), zone_->GetSlotIndex(), paramIndex, buffer, bufferSize);
    }    
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Zone
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Zone::Deactivate()
{
    for(auto widget : widgets_)
        widget->Deactivate();
    
    widgets_.clear();
    
    // GAW TBD Leaving Zone - if needed
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZoneTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ZoneTemplate::ProcessWidgetActionTemplates(ControlSurface* surface, Zone* zone, string channelNumStr, bool shouldUseNoAction)
{
    for(auto  widgetActionTemplate :  widgetActionTemplates)
    {
        string widgetName = regex_replace(widgetActionTemplate->widgetName, regex("[|]"), channelNumStr);
        
        if(Widget* widget = surface->GetWidgetByName(widgetName))
        {
            if(widgetActionTemplate->isModifier)
                widget->SetIsModifier();
            
            WidgetContext widgetContext = WidgetContext(widget, zone);
            
            for(auto aTemplate : widgetActionTemplate->actionContextTemplates)
            {
                for(auto member : aTemplate->members)
                {
                    string actionName = regex_replace(member->actionName, regex("[|]"), channelNumStr);
                    vector<string> memberParams;
                    for(int i = 0; i < member->params.size(); i++)
                        memberParams.push_back(regex_replace(member->params[i], regex("[|]"), channelNumStr));
                    
                    
                    
                    /*
                    if(shouldUseNoAction)
                    {
                        ActionContext context = TheManager->GetActionContext("NoAction", widget, zone, memberParams);
                        member->SetProperties(context);
                        widgetContext.AddActionContext(aTemplate->modifier, context);
                    }
                    else
                    {
                        ActionContext context = TheManager->GetActionContext(actionName, widget, zone, memberParams);
                        member->SetProperties(context);
                        widgetContext.AddActionContext(aTemplate->modifier, context);
                    }
                    */
                    

                    ActionContext context = TheManager->GetActionContext(shouldUseNoAction == true ? "NoAction" : actionName, widget, zone, memberParams, member->properties);
                    
                    if(member->isFeedbackInverted)
                        context.SetIsFeedbackInverted();
                    
                    if(member->holdDelayAmount != 0.0)
                        context.SetHoldDelayAmount(member->holdDelayAmount);
                    
                    widgetContext.AddActionContext(aTemplate->modifier, context);
                }
            }
            
            zone->AddWidget(widget);
            widget->Activate(widgetContext);
        }
    }
}

void ZoneTemplate::Activate(ControlSurface* surface, vector<Zone*> &activeZones)
{
    for(auto includedZoneTemplateStr : includedZoneTemplates)
        if(ZoneTemplate* includedZoneTemplate = surface->GetZoneTemplate(includedZoneTemplateStr))
            includedZoneTemplate->Activate(surface, activeZones);

    for(int i = 0; i < navigators.size(); i++)
    {
        string newZoneName = name;
        
        string channelNumStr = to_string(i + 1);

        if(navigators.size() > 1)
            newZoneName += channelNumStr;
        
        surface->LoadingZone(newZoneName);
        
        Zone* zone = new Zone(surface, navigators[i], newZoneName, alias, sourceFilePath);

        ProcessWidgetActionTemplates(surface, zone, channelNumStr, false);

        activeZones.push_back(zone);
    }
}

void ZoneTemplate::Activate(ControlSurface*  surface, vector<Zone*> &activeZones, int maxNum)
{
    for(auto includedZoneTemplateStr : includedZoneTemplates)
        if(ZoneTemplate* includedZoneTemplate = surface->GetZoneTemplate(includedZoneTemplateStr))
            includedZoneTemplate->Activate(surface, activeZones, maxNum);
    
    for(int i = 0; i < navigators.size(); i++)
    {
        string newZoneName = name;
        
        string zoneNumStr = to_string(i + 1);
        
        if(navigators.size() > 1)
            newZoneName += zoneNumStr;
        
        surface->LoadingZone(newZoneName);
        
        Zone* zone = new Zone(surface, navigators[i], newZoneName, alias, sourceFilePath);
        
        zone->SetSlotIndex(i);
        
        if(i < maxNum)
            ProcessWidgetActionTemplates(surface, zone, zoneNumStr, false);
        else
            ProcessWidgetActionTemplates(surface, zone, zoneNumStr, true);
        
        activeZones.push_back(zone);
    }
}

void ZoneTemplate::Activate(ControlSurface*  surface, vector<Zone*> &activeZones, int slotIndex, bool shouldUseNoAction)
{
    for(auto includedZoneTemplateStr : includedZoneTemplates)
        if(ZoneTemplate* includedZoneTemplate = surface->GetZoneTemplate(includedZoneTemplateStr))
            includedZoneTemplate->Activate(surface, activeZones, slotIndex, shouldUseNoAction);
    
    if(navigators.size() == 1)
    {
        surface->LoadingZone(name);
        
        Zone* zone = new Zone(surface, navigators[0], name, alias, sourceFilePath);
        
        zone->SetSlotIndex(slotIndex);
        
        ProcessWidgetActionTemplates(surface, zone, "", shouldUseNoAction);
        
        activeZones.push_back(zone);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ActionTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////
// Widget
////////////////////////////////////////////////////////////////////////////////////////////////////////
Widget::Widget(ControlSurface* surface, string name) : surface_(surface), name_(name),
                currentWidgetContext_(WidgetContext(this, GetSurface()->GetDefaultZone())),
                defaultWidgetContext_(WidgetContext(this, GetSurface()->GetDefaultZone())) {}

Widget::~Widget()
{
    for(auto feedbackProcessor :feedbackProcessors_)
    {
        delete feedbackProcessor;
        feedbackProcessor = nullptr;
    }
};

void Widget::SetProperties(vector<vector<string>> properties)
{
    for(auto processor : feedbackProcessors_)
        processor->SetProperties(properties);
}

void  Widget::UpdateValue(double value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(value);
}

void  Widget::UpdateValue(int mode, double value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(mode, value);
}

void  Widget::UpdateValue(string value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(value);
}

void Widget::UpdateValueAndColors(string value, rgb_color textColor, rgb_color textBackground)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValueAndColors(value, textColor, textBackground);
}

void  Widget::UpdateRGBValue(int r, int g, int b)
{
    for(auto processor : feedbackProcessors_)
        processor->SetRGBValue(r, g, b);
}

void  Widget::ForceValue(double value)
{
    for(auto processor : feedbackProcessors_)
        processor->ForceValue(value);
}

void  Widget::ForceValue(int mode, double value)
{
    for(auto processor : feedbackProcessors_)
        processor->ForceValue(mode, value);
}

void  Widget::ForceValue(string value)
{
    for(auto processor : feedbackProcessors_)
        processor->ForceValue(value);
}

void Widget::ForceValueAndColors(string value, rgb_color textColor, rgb_color textBackground)
{
    for(auto processor : feedbackProcessors_)
        processor->ForceValueAndColors(value, textColor, textBackground);
}

void  Widget::ForceRGBValue(int r, int g, int b)
{
    for(auto processor : feedbackProcessors_)
        processor->ForceRGBValue(r, g, b);
}

void  Widget::Clear()
{
    for(auto processor : feedbackProcessors_)
        processor->Clear();
}

void  Widget::ForceClear()
{
    for(auto processor : feedbackProcessors_)
        processor->ForceClear();
}

void Widget::ClearCache()
{
    for(auto processor : feedbackProcessors_)
        processor->ClearCache();
}

void Widget::LogInput(double value)
{
    if( TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s %f\n", GetSurface()->GetName().c_str(), GetName().c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CSIMessageGenerator::CSIMessageGenerator(ControlSurface* surface, Widget* widget, string message) : widget_(widget)
{
    surface->AddCSIMessageGenerator(message, this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_FeedbackProcessor::SendMidiMessage(MIDI_event_ex_t* midiMessage)
{
    surface_->SendMidiMessage(midiMessage);
}

void Midi_FeedbackProcessor::SendMidiMessage(int first, int second, int third)
{
    if(first != lastMessageSent_->midi_message[0] || second != lastMessageSent_->midi_message[1] || third != lastMessageSent_->midi_message[2])
        ForceMidiMessage(first, second, third);
}

void Midi_FeedbackProcessor::ForceMidiMessage(int first, int second, int third)
{
    lastMessageSent_->midi_message[0] = first;
    lastMessageSent_->midi_message[1] = second;
    lastMessageSent_->midi_message[2] = third;
    surface_->SendMidiMessage(first, second, third);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void OSC_FeedbackProcessor::ForceValue(double value)
{
    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::ForceValue(int param, double value)
{
    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::ForceValue(string value)
{
    lastStringValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// EuCon_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void EuCon_FeedbackProcessor::ForceValue(double value)
{
    lastDoubleValue_ = value;
    surface_->SendEuConMessage(this, address_, value);
}

void EuCon_FeedbackProcessor::ForceValue(int param, double value)
{
    lastDoubleValue_ = value;
    surface_->SendEuConMessage(this, address_, value, param);
}

void EuCon_FeedbackProcessor::ForceValue(string value)
{
    lastStringValue_ = value;
    surface_->SendEuConMessage(this, address_, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// EuCon_FeedbackProcessorDB
////////////////////////////////////////////////////////////////////////////////////////////////////////
void EuCon_FeedbackProcessorDB::Clear()
{
    if(lastDoubleValue_ != -100.0)
        ForceClear();
}

void EuCon_FeedbackProcessorDB::ForceClear()
{
    lastDoubleValue_ = -100.0;
    surface_->SendEuConMessage(this, address_, -100.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// FXActivationManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void FXActivationManager::MapSelectedTrackFXToMenu()
{
    DeactivateSelectedTrackFXMenuZones();
    DeactivateSelectedTrackFXMenuFXZones();
    
    if(MediaTrack* track = surface_->GetPage()->GetTrackNavigationManager()->GetSelectedTrack())
    {
        int numFX = DAW::TrackFX_GetCount(track);
        
        if(surface_->GetZoneTemplate("FXMenu") != nullptr)
            surface_->GetZoneTemplate("FXMenu")->Activate(surface_, activeSelectedTrackFXMenuFXZones_, numFX);
    }
}

void FXActivationManager::MapSelectedTrackFXToWidgets()
{
    DeactivateSelectedTrackFXZones();
    
    if(MediaTrack* selectedTrack = surface_->GetPage()->GetTrackNavigationManager()->GetSelectedTrack())
        for(int i = 0; i < DAW::TrackFX_GetCount(selectedTrack); i++)
            MapSelectedTrackFXSlotToWidgets(selectedTrack, i);
}

void FXActivationManager::MapSelectedTrackFXSlotToWidgets(MediaTrack* selectedTrack, int fxSlot)
{
    char FXName[BUFSZ];
    
    DAW::TrackFX_GetFXName(selectedTrack, fxSlot, FXName, sizeof(FXName));
    
    if(ZoneTemplate* zoneTemplate = surface_->GetZoneTemplate(FXName))
    {
        if(zoneTemplate->navigators.size() == 1 && ! zoneTemplate->navigators[0]->GetIsFocusedFXNavigator())
            zoneTemplate->Activate(surface_, activeSelectedTrackFXZones_, fxSlot, false);
    }
}

void FXActivationManager::MapFocusedFXToWidgets()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int fxSlot = 0;
    MediaTrack* focusedTrack = nullptr;
    
    if(DAW::GetFocusedFX(&trackNumber, &itemNumber, &fxSlot) == 1)
        if(trackNumber > 0)
            focusedTrack = surface_->GetPage()->GetTrackNavigationManager()->GetTrackFromId(trackNumber);
    
    DeactivateFocusedFXZones();
    
    char FXName[BUFSZ];
    DAW::TrackFX_GetFXName(focusedTrack, fxSlot, FXName, sizeof(FXName));
    
    if(ZoneTemplate* zoneTemplate = surface_->GetZoneTemplate(FXName))
    {
        if(zoneTemplate->navigators.size() == 1 && zoneTemplate->navigators[0]->GetIsFocusedFXNavigator())
            zoneTemplate->Activate(surface_, activeFocusedFXZones_, fxSlot, false);
    }
}

void FXActivationManager::TrackFXListChanged()
{
    surface_->OnTrackSelection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
ControlSurface::ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset) :  CSurfIntegrator_(CSurfIntegrator), page_(page), name_(name), zoneFolder_(zoneFolder), fxActivationManager_(new FXActivationManager(this, numFX)), numChannels_(numChannels), numSends_(numSends), defaultZone_(new Zone(this, GetPage()->GetDefaultNavigator(), "Default", "Default", ""))
{
    for(int i = 0; i < numChannels; i++)
        navigators_[i] = GetPage()->GetTrackNavigationManager()->GetNavigatorForChannel(i + channelOffset);
}

void ControlSurface::InitZones(string zoneFolder)
{
    try
    {
        vector<string> zoneFilesToProcess;
        listZoneFiles(DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder + "/", zoneFilesToProcess); // recursively find all the .zon files, starting at zoneFolder
        
        for(auto zoneFilename : zoneFilesToProcess)
            ProcessZoneFile(zoneFilename, this);
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble parsing Zone folders\n");
        DAW::ShowConsoleMsg(buffer);
    }
}

Navigator* ControlSurface::GetNavigatorForChannel(int channelNum)
{
    if(channelNum < 0)
        return nullptr;
    
    if(navigators_.count(channelNum) > 0)
        return navigators_[channelNum];
    else
        return nullptr;
}

void ControlSurface::MapSelectedTrackSendsToWidgets()
{
    DeactivateSendsZones();
    
    if(MediaTrack* track = GetPage()->GetTrackNavigationManager()->GetSelectedTrack())
    {
        int numSends = DAW::GetTrackNumSends(track, 0);
        
        if(zoneTemplates_.count("Send") > 0)
            zoneTemplates_["Send"]->Activate(this, activeSendZones_, numSends);
    }
}

void ControlSurface::SurfaceOutMonitor(Widget* widget, string address, string value)
{
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + address + " " + value + "\n").c_str());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_ControlSurface::InitWidgets(string templateFilename, string zoneFolder)
{
    ProcessWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/Midi/" + templateFilename, this);
    InitHardwiredWidgets();
    Initialize();
    InitZones(zoneFolder);
    MakeHomeDefault();
    ForceClearAllWidgets();
    GetPage()->ForceRefreshTimeDisplay();
}

void Midi_ControlSurface::ProcessMidiMessage(const MIDI_event_ex_t* evt)
{
    bool isMapped = false;
    
    // At this point we don't know how much of the message comprises the key, so try all three
    if(Midi_CSIMessageGeneratorsByMessage_.count(evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100 + evt->midi_message[2]) > 0)
    {
        isMapped = true;
        for( auto generator : Midi_CSIMessageGeneratorsByMessage_[evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100 + evt->midi_message[2]])
            generator->ProcessMidiMessage(evt);
    }
    else if(Midi_CSIMessageGeneratorsByMessage_.count(evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100) > 0)
    {
        isMapped = true;
        for( auto generator : Midi_CSIMessageGeneratorsByMessage_[evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100])
            generator->ProcessMidiMessage(evt);
    }
    else if(Midi_CSIMessageGeneratorsByMessage_.count(evt->midi_message[0] * 0x10000) > 0)
    {
        isMapped = true;
        for( auto generator : Midi_CSIMessageGeneratorsByMessage_[evt->midi_message[0] * 0x10000])
            generator->ProcessMidiMessage(evt);
        
    }
    
    if( ! isMapped && TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %02x  %02x  %02x \n", name_.c_str(), evt->midi_message[0], evt->midi_message[1], evt->midi_message[2]);
        DAW::ShowConsoleMsg(buffer);
        
    }
}

void Midi_ControlSurface::SendMidiMessage(MIDI_event_ex_t* midiMessage)
{
    if(midiOutput_)
        midiOutput_->SendMsg(midiMessage, -1);
    
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " SysEx\n").c_str());
}

void Midi_ControlSurface::SendMidiMessage(int first, int second, int third)
{
    if(midiOutput_)
        midiOutput_->Send(first, second, third, -1);
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "%s  %02x  %02x  %02x \n", ("OUT->" + name_).c_str(), first, second, third);
        DAW::ShowConsoleMsg(buffer);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
void OSC_ControlSurface::InitWidgets(string templateFilename, string zoneFolder)
{
    ProcessWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/OSC/" + templateFilename, this);
    
    InitHardwiredWidgets();
    InitZones(zoneFolder);
    MakeHomeDefault();
    ForceClearAllWidgets();
    GetPage()->ForceRefreshTimeDisplay();
}

void OSC_ControlSurface::ProcessOSCMessage(string message, double value)
{
    if(CSIMessageGeneratorsByMessage_.count(message) > 0)
        CSIMessageGeneratorsByMessage_[message]->ProcessMessage(value);
    
    if(TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s  %f  \n", name_.c_str(), message.c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

void OSC_ControlSurface::LoadingZone(string zoneName)
{
    string oscAddress(zoneName);
    oscAddress = regex_replace(oscAddress, regex(BadFileChars), "_");
    oscAddress = "/" + oscAddress;

    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg((zoneName + "->" + "LoadingZone---->" + name_ + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, double value)
{
    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress).pushFloat(value);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        if(TheManager->GetSurfaceOutDisplay())
            DAW::ShowConsoleMsg(("OUT->" + name_ + " " + oscAddress + " " + to_string(value) + "\n").c_str());
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, string value)
{
    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress).pushStr(value);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    SurfaceOutMonitor(feedbackProcessor->GetWidget(), oscAddress, value);
    
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
// For EuCon

void EuConRequestsInitialization()
{
    if(TheManager)
        TheManager->InitializeEuCon();
}

void InitializeEuConWidgets(EuCon_ControlSurface* surface, vector<CSIWidgetInfo> *assemblyInfoItems)
{
    surface->InitializeEuConWidgets(assemblyInfoItems);
}

void HandleEuConMessageWithDouble(EuCon_ControlSurface* surface, const char *address, double value)
{
    surface->HandleEuConMessage(string(address), value);
}

void HandleEuConGroupVisibilityChange(EuCon_ControlSurface* surface, const char *groupName, int channelNumber, bool isVisible)
{
    surface->HandleEuConGroupVisibilityChange(string(groupName), channelNumber, isVisible);
}

void HandleEuConGetMeterValues(EuCon_ControlSurface* surface, int id, int iLeg, float* oLevel, float* oPeak, bool* oLegClip)
{
    surface->HandleEuConGetMeterValues(id, iLeg, oLevel, oPeak, oLegClip);
}

void HandleEuConGetFormattedFXParamValue(EuCon_ControlSurface* surface, const char* address, char *buffer, int bufferSize)
{
    surface->HandleEuConGetFormattedFXParamValue(address, buffer, bufferSize);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// EuCon_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
EuCon_ControlSurface::EuCon_ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset)
: ControlSurface(CSurfIntegrator, page, name, zoneFolder, numChannels, numSends, numFX, channelOffset)
{
    if( ! plugin_register("API_EuConRequestsInitialization", (void *)::EuConRequestsInitialization))
        LOG::InitializationFailure("EuConRequestsInitialization failed to register");

    if( ! plugin_register("API_InitializeEuConWidgets", (void *)::InitializeEuConWidgets))
        LOG::InitializationFailure("InitializeEuConWidgets failed to register");
   
    if( ! plugin_register("API_HandleEuConMessageWithDouble", (void *)::HandleEuConMessageWithDouble))
        LOG::InitializationFailure("HandleEuConMessageWithDouble failed to register");
    
    if( ! plugin_register("API_HandleEuConGroupVisibilityChange", (void *)::HandleEuConGroupVisibilityChange))
        LOG::InitializationFailure("HandleEuConGroupVisibilityChange failed to register");
    
    if( ! plugin_register("API_HandleEuConGetMeterValues", (void *)::HandleEuConGetMeterValues))
        LOG::InitializationFailure("HandleEuConGetMeterValues failed to register");
    
    if( ! plugin_register("API_HandleEuConGetFormattedFXParamValue", (void *)::HandleEuConGetFormattedFXParamValue))
        LOG::InitializationFailure("HandleEuConGetFormattedFXParamValue failed to register");
    
    InitializeEuCon();
}

void EuCon_ControlSurface::InitializeEuCon()
{
    static void (*InitializeEuConWithParameters)(EuCon_ControlSurface* surface, int numChannels, int numSends, int numFX, int panOptions) = nullptr;

    if(g_reaper_plugin_info && InitializeEuConWithParameters == nullptr)
        InitializeEuConWithParameters = (void (*)(EuCon_ControlSurface*, int, int, int, int))g_reaper_plugin_info->GetFunc("InitializeEuConWithParameters");

    int panOptions = TheManager->GetProjectPanMode() == 6 ? 2 : 1;
    
    if(InitializeEuConWithParameters)
        InitializeEuConWithParameters(this, numChannels_, numSends_, fxActivationManager_->GetNumFXSlots(), panOptions);
}

Widget*  EuCon_ControlSurface::InitializeEuConWidget(CSIWidgetInfo &widgetInfo)
{
    if(widgetInfo.name != "")
    {
        Widget* widget = new Widget(this, widgetInfo.name);
        
        if(!widget)
            return nullptr;
        
        if(widgetInfo.control != "")
            new CSIMessageGenerator(this, widget, widgetInfo.control);
        
        if(widgetInfo.touch != "")
            new Touch_CSIMessageGenerator(this, widget, widgetInfo.touch);
        
        if(widgetInfo.FB_Processor != "")
        {
            if(widgetInfo.FB_Processor.find("FaderDB") != string::npos)
                widget->AddFeedbackProcessor(new EuCon_FeedbackProcessorDB(this, widget, widgetInfo.FB_Processor));
            else
                widget->AddFeedbackProcessor(new EuCon_FeedbackProcessor(this, widget, widgetInfo.FB_Processor));
        }
        
        return widget;
    }
    
    return nullptr;
}

void EuCon_ControlSurface::InitializeEuConWidgets(vector<CSIWidgetInfo> *widgetInfoItems)
{
    for(auto item : *widgetInfoItems)
    {
        if(Widget* widget = InitializeEuConWidget(item))
        {
            AddWidget(widget);
            
            if(item.channelNumber > 0 && channelGroups_.count(item.channelNumber) < 1 )
                channelGroups_[item.channelNumber] = new WidgetGroup();

            if(item.group == "General")
                generalWidgets_.push_back(widget);
            
            if(channelGroups_.count(item.channelNumber) > 0)
            {
                if(item.group == "Channel")
                    channelGroups_[item.channelNumber]->AddWidget(widget);
                else
                    channelGroups_[item.channelNumber]->AddWidgetToSubgroup(item.group, widget);
            }
        }
    }
    
    InitHardwiredWidgets();
    InitZones(zoneFolder_);
    MakeHomeDefault();
    ForceClearAllWidgets();
    GetPage()->ForceRefreshTimeDisplay();
}

void EuCon_ControlSurface::SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, double value)
{
    static void (*HandleReaperMessageWthDouble)(const char *, double) = nullptr;
    
    if(g_reaper_plugin_info && HandleReaperMessageWthDouble == nullptr)
        HandleReaperMessageWthDouble = (void (*)(const char *, double))g_reaper_plugin_info->GetFunc("HandleReaperMessageWthDouble");
    
    if(HandleReaperMessageWthDouble)
        HandleReaperMessageWthDouble(address.c_str(), value);
    
    SurfaceOutMonitor(feedbackProcessor->GetWidget(), address, to_string(value));
}

void EuCon_ControlSurface::SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, double value, int param)
{
    static void (*HandleReaperMessageWthParam)(const char *, double, int) = nullptr;
    
    if(g_reaper_plugin_info && HandleReaperMessageWthParam == nullptr)
        HandleReaperMessageWthParam = (void (*)(const char *, double, int))g_reaper_plugin_info->GetFunc("HandleReaperMessageWthParam");
    
    if(HandleReaperMessageWthParam)
        HandleReaperMessageWthParam(address.c_str(), value, param);
    
    SurfaceOutMonitor(feedbackProcessor->GetWidget(), address, to_string(value));
}

void EuCon_ControlSurface::SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, string value)
{
    if(address.find("Pan_Display") != string::npos
       || address.find("Width_Display") != string::npos
       || address.find("PanL_Display") != string::npos
       || address.find("PanR_Display") != string::npos)
    {
        return; // GAW -- Hack to prevent overwrite of Pan, Width, etc. labels
    }
    
    static void (*HandleReaperMessageWthString)(const char *, const char *) = nullptr;
    
    if(g_reaper_plugin_info && HandleReaperMessageWthString == nullptr)
        HandleReaperMessageWthString = (void (*)(const char *, const char *))g_reaper_plugin_info->GetFunc("HandleReaperMessageWithString");
    
    if(HandleReaperMessageWthString)
        HandleReaperMessageWthString(address.c_str(), value.c_str());
    
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT-> " + name_ + " " + address + " " + value + "\n").c_str());
}

void EuCon_ControlSurface::SendEuConMessage(string address, string value)
{
    static void (*HandleReaperMessageWthString)(const char *, const char *) = nullptr;
    
    if(g_reaper_plugin_info && HandleReaperMessageWthString == nullptr)
        HandleReaperMessageWthString = (void (*)(const char *, const char *))g_reaper_plugin_info->GetFunc("HandleReaperMessageWithString");
    
    if(HandleReaperMessageWthString)
        HandleReaperMessageWthString(address.c_str(), value.c_str());
}

void EuCon_ControlSurface::HandleEuConGetMeterValues(int id, int iLeg, float* oLevel, float* oPeak, bool* oLegClip)
{
    if(MediaTrack* track = GetPage()->GetTrackNavigationManager()->GetTrackFromChannel(id))
    {
        float left = VAL2DB(DAW::Track_GetPeakInfo(track, 0));
        float right = VAL2DB(DAW::Track_GetPeakInfo(track, 1));
        
        *oLevel = (left + right) / 2.0;
       
        float max = left > right ? left : right;
        
        if(peakInfo_.count(id) > 0 && peakInfo_[id].peakValue < max)
        {
            peakInfo_[id].timePeakSet  = DAW::GetCurrentNumberOfMilliseconds();
            peakInfo_[id].peakValue = max;
            if(max > 0.0)
                peakInfo_[id].isClipping = true;
        }
        
        if(peakInfo_.count(id) < 1)
        {
            peakInfo_[id].timePeakSet  = DAW::GetCurrentNumberOfMilliseconds();
            peakInfo_[id].peakValue = max;
            if(max > 0.0)
                peakInfo_[id].isClipping = true;
        }
        
        if(peakInfo_.count(id) > 0 && (DAW::GetCurrentNumberOfMilliseconds() - peakInfo_[id].timePeakSet > 2000))
        {
            peakInfo_[id].timePeakSet  = DAW::GetCurrentNumberOfMilliseconds();
            peakInfo_[id].peakValue = max;
            peakInfo_[id].isClipping = false;
        }
        
        *oPeak = peakInfo_[id].peakValue;
        *oLegClip = peakInfo_[id].isClipping;
    }
    else
    {
        *oLevel = -144.0;
        *oPeak = -144.0;
        *oLegClip = false;
    }
}

void EuCon_ControlSurface::HandleEuConGetFormattedFXParamValue(const char* address, char *buffer, int bufferSize)
{
    if(widgetsByName_.count(address) > 0)
        widgetsByName_[address]->GetFormattedFXParamValue(buffer, bufferSize);
}

void EuCon_ControlSurface::HandleEuConGroupVisibilityChange(string groupName, int channelNumber, bool isVisible)
{
    if(groupName == "FX")
    {
        
        if(isVisible && widgetsByName_.count("OnEuConFXAreaGainedFocus") > 0)
        {
            isEuConFXAreaFocused_ = true;
            widgetsByName_["OnEuConFXAreaGainedFocus"]->DoAction(1.0);
        }
        
        if( ! isVisible && widgetsByName_.count("OnEuConFXAreaLostFocus") > 0)
        {
            isEuConFXAreaFocused_ = false;
            widgetsByName_["OnEuConFXAreaLostFocus"]->DoAction(1.0);
        }
    }
    
    if(groupName == "FX")
        for(auto [channel, group] : channelGroups_)
            group->SetIsVisible("FX", isVisible);
    
    if(groupName == "Pan")
        for(auto [channel, group] : channelGroups_)
            group->SetIsVisible("Pan", isVisible);
    
    else if(groupName == "Send")
        for(auto [channel, group] : channelGroups_)
            group->SetIsVisible("Send", isVisible);
    
    else if(groupName == "Channel" && channelGroups_.count(channelNumber) > 0)
        channelGroups_[channelNumber]->SetIsVisible(isVisible);
}

void EuCon_ControlSurface::HandleEuConMessage(string address, double value)
{
    if(address == "PostMessage" && g_hwnd != nullptr)
        PostMessage(g_hwnd, WM_COMMAND, (int)value, 0);
    else if(address == "LayoutChanged")
        DAW::MarkProjectDirty(nullptr);
    else if(CSIMessageGeneratorsByMessage_.count(address) > 0)
        CSIMessageGeneratorsByMessage_[address]->ProcessMessage(value);
        
    if(TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s  %f  \n", name_.c_str(), address.c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

void EuCon_ControlSurface::UpdateTimeDisplay()
{
    double playPosition = (GetPlayState() & 1 ) ? GetPlayPosition() : GetCursorPosition();
    
    if(previousPP != playPosition) // GAW :) Yeah I know shouldn't compare FP values, but the worst you get is an extra update or two, meh.
    {
        previousPP = playPosition;

        int *timeModePtr = TheManager->GetTimeMode2Ptr(); // transport
        
        int timeMode = 0;
        
        if (timeModePtr && (*timeModePtr) >= 0)
            timeMode = *timeModePtr;
        else
        {
            timeModePtr = TheManager->GetTimeModePtr(); // ruler
            
            if (timeModePtr)
                timeMode = *timeModePtr;
        }
        
        char samplesBuf[64];
        memset(samplesBuf, 0, sizeof(samplesBuf));
        char measuresBuf[64];
        memset(measuresBuf, 0, sizeof(measuresBuf));
        char chronoBuf[64];
        memset(chronoBuf, 0, sizeof(chronoBuf));

        if(timeMode == 4)  // Samples
        {
            format_timestr_pos(playPosition, samplesBuf, sizeof(samplesBuf), timeMode);
        }
        
        if(timeMode == 1 || timeMode == 2)  // Bars/Beats/Ticks
        {
            int num_measures = 0;
            double beats = TimeMap2_timeToBeats(NULL, playPosition, &num_measures, NULL, NULL, NULL) + 0.000000000001;
            double nbeats = floor(beats);
            beats -= nbeats;
            format_timestr_pos(playPosition, measuresBuf, sizeof(measuresBuf), 2);
        }
        
        if(timeMode == 0 || timeMode == 1 || timeMode == 3  ||  timeMode == 5)  // Hours/Minutes/Seconds/Frames
        {
            double *timeOffsetPtr = TheManager->GetTimeOffsPtr();
            if (timeOffsetPtr)
                playPosition += (*timeOffsetPtr);
            format_timestr_pos(playPosition, chronoBuf, sizeof(chronoBuf), timeMode == 1 ? 0 : timeMode);
        }
        
        switch(timeMode)
        {
            case 0: // Hours/Minutes/Seconds
            case 3: // Seconds
            case 5: // Hours/Minutes/Seconds/Frames
                SendEuConMessage("PrimaryTimeDisplay", chronoBuf);
                SendEuConMessage("SecondaryTimeDisplay", "");
                break;
                
            case 1:
                SendEuConMessage("PrimaryTimeDisplay", measuresBuf);
                SendEuConMessage("SecondaryTimeDisplay", chronoBuf);
                break;
                
            case 2:
                SendEuConMessage("PrimaryTimeDisplay", measuresBuf);
                SendEuConMessage("SecondaryTimeDisplay", "");
                break;
                
            case 4:
                SendEuConMessage("PrimaryTimeDisplay", samplesBuf);
                SendEuConMessage("SecondaryTimeDisplay", "");
                break;
        }
    }
}

void Midi_ControlSurface::InitializeMCU()
{
    vector<vector<int>> sysExLines;
    /*
    sysExLines.push_back({0xF0, 0x15, 0x29, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x10, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x11, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x17, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x17, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x15, 0x10, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x00, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x01, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x02, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x03, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x04, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x05, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x06, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x07, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0A, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x00, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x01, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x02, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x03, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x04, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x05, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x06, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x07, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x08, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x21, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x00, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x01, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x02, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x03, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x04, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x05, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x06, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x07, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0A, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0A, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x21, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x00, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x01, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x02, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x03, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x04, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x05, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x06, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x07, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0A, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x00, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x01, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x02, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x03, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x04, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x05, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x06, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x07, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x0E, 0x08, 0x03, 0xF7});
    */
    
    sysExLines.push_back({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x21, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x00, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x01, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x02, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x03, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x04, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x05, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x07, 0x01, 0xF7});
    
    struct
    {
        MIDI_event_ex_t evt;
        char data[BUFSZ];
    } midiSysExData;
    
    for(auto line : sysExLines)
    {
        memset(midiSysExData.data, 0, sizeof(midiSysExData.data));
        
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;

        for(auto value : line)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = value;
        
        SendMidiMessage(&midiSysExData.evt);
    }
}

void Midi_ControlSurface::InitializeMCUXT()
{
    vector<vector<int>> sysExLines;
    
    /*
    sysExLines.push_back({0xF0, 0x15, 0x29, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x10, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x11, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x17, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x17, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x15, 0x10, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x00, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x01, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x02, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x03, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x04, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x05, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x06, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x07, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0A, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x00, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x01, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x02, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x03, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x04, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x05, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x06, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x07, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x08, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x12, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x21, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x00, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x01, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x02, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x03, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x04, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x05, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x06, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x07, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0A, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0A, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x12, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x13, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x12, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x21, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x00, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x01, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x02, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x03, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x04, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x05, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x06, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x07, 0x07, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0A, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0C, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0B, 0x0F, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x00, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x01, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x02, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x03, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x04, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x05, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x06, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x07, 0x03, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x0E, 0x08, 0x03, 0xF7});
    */
    
    sysExLines.push_back({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x21, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x00, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x01, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x02, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x03, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x04, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x05, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x07, 0x01, 0xF7});
    
    struct
    {
        MIDI_event_ex_t evt;
        char data[BUFSZ];
    } midiSysExData;
    
    for(auto line : sysExLines)
    {
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        
        for(auto value : line)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = value;
        
        SendMidiMessage(&midiSysExData.evt);
    }

}

