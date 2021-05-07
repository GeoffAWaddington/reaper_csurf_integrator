//
//  control_surface_integrator.h
//  reaper_control_surface_integrator
//
//

//  Note for Windows environments:
//  use std::byte for C++17 byte
//  use ::byte for Windows byte

#ifndef control_surface_integrator
#define control_surface_integrator

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include "time.h"
#include <sstream>
#include <vector>
#include <map>
#include <iomanip>
#include <fstream>
#include <regex>
#include <cmath>

#ifdef _WIN32
#include "oscpkt.hh"
#include "udp.hh"
#endif

#include "control_surface_integrator_Reaper.h"

#ifdef _WIN32
#include <memory>
#include "direntWin.h"
#include <functional>
#include "commctrl.h"
#else
#include <dirent.h>
#include "oscpkt.hh"
#include "udp.hh"
#endif

extern string GetLineEnding();

extern REAPER_PLUGIN_HINSTANCE g_hInst;

const string ControlSurfaceIntegrator = "ControlSurfaceIntegrator";

const string FollowMCPToken = "FollowMCP";
const string MidiSurfaceToken = "MidiSurface";
const string OSCSurfaceToken = "OSCSurface";
const string EuConSurfaceToken = "EuConSurface";
const string PageToken = "Page";

const string Shift = "Shift";
const string Option = "Option";
const string Control = "Control";
const string Alt = "Alt";

const string BadFileChars = "[ \\:*?<>|.,()/]";
const string CRLFChars = "[\r\n]";
const string TabChars = "[\t]";

const int TempDisplayTime = 1250;

class Manager;
extern Manager* TheManager;

static vector<string> GetTokens(string line)
{
    vector<string> tokens;
    
    istringstream iss(line);
    string token;
    while (iss >> quoted(token))
        tokens.push_back(token);
    
    return tokens;
}

struct CSIWidgetInfo
{
    std::string group = "General";
    int channelNumber = 0;
    int sendNumber = 0;
    bool isVisible = true;
    std::string name = "";
    std::string control = "";
    std::string FB_Processor = "";
    std::string touch = "";
    
    CSIWidgetInfo(std::string aName, std::string aControl, std::string aFB_Processor) : CSIWidgetInfo(aName, aControl, aFB_Processor, "General", 0, 0, true) {}
    
    CSIWidgetInfo(std::string aName, std::string aControl, std::string aFB_Processor, std::string aGroup, int aChannelNumber, bool isVisible) :  CSIWidgetInfo(aName, aControl, aFB_Processor, aGroup, aChannelNumber, 0, isVisible) {}
    
    CSIWidgetInfo(std::string aName, std::string aControl, std::string aFB_Processor, std::string aTouchName, std::string aGroup, int aChannelNumber, bool isVisible) :  CSIWidgetInfo(aName, aControl, aFB_Processor, aTouchName, aGroup, aChannelNumber, 0, isVisible) {}
    
    CSIWidgetInfo(std::string aName, std::string aControl, std::string aFB_Processor, std::string aGroup, int aChannelNumber, int aSendNumber, bool itemIsVisible) :  name(aName), control(aControl), FB_Processor(aFB_Processor), group(aGroup), channelNumber(aChannelNumber), sendNumber(aSendNumber), isVisible(itemIsVisible) {}
    
    CSIWidgetInfo(std::string aName, std::string aControl, std::string aFB_Processor, std::string aTouchName, std::string aGroup, int aChannelNumber, int aSendNumber, bool itemIsVisible) :  name(aName), control(aControl), FB_Processor(aFB_Processor), touch(aTouchName), group(aGroup), channelNumber(aChannelNumber), sendNumber(aSendNumber), isVisible(itemIsVisible) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSurfIntegrator;
class Page;
class ControlSurface;
class Midi_ControlSurface;
class OSC_ControlSurface;
class EuCon_ControlSurface;
class Widget;
class MappedWidgetsNavigationManager;
class TrackNavigationManager;
class FeedbackProcessor;
class Zone;
class ActionContext;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Action() {}
    
    virtual string GetName() { return "Action"; }
    virtual void RequestUpdate(ActionContext* context) {}
    virtual void Do(ActionContext* context, double value) {}
    virtual void Touch(ActionContext* context, double value) {}
    virtual double GetCurrentNormalizedValue(ActionContext* context) { return 0.0; }
    virtual double GetCurrentDBValue(ActionContext* context) { return 0.0; }

    int GetPanMode(MediaTrack* track)
    {
        double pan1, pan2 = 0.0;
        int panMode = 0;
        DAW::GetTrackUIPan(track, &pan1, &pan2, &panMode);
        return panMode;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Page* const page_ = nullptr;
    bool isVolumeTouched_ = false;
    bool isPanTouched_ = false;
    bool isPanWidthTouched_ = false;
    bool isPanLeftTouched_ = false;
    bool isPanRightTouched_ = false;

public:
    Navigator(Page*  page) : page_(page) {}
    virtual ~Navigator() {}
    
    void SetIsVolumeTouched(bool isVolumeTouched) { isVolumeTouched_ = isVolumeTouched;  }
    bool GetIsVolumeTouched() { return isVolumeTouched_;  }
    
    void SetIsPanTouched(bool isPanTouched) { isPanTouched_ = isPanTouched; }
    bool GetIsPanTouched() { return isPanTouched_;  }
    
    void SetIsPanWidthTouched(bool isPanWidthTouched) { isPanWidthTouched_ = isPanWidthTouched; }
    bool GetIsPanWidthTouched() { return isPanWidthTouched_;  }
    
    void SetIsPanLeftTouched(bool isPanLeftTouched) { isPanLeftTouched_ = isPanLeftTouched; }
    bool GetIsPanLeftTouched() { return isPanLeftTouched_;  }
    
    void SetIsPanRightTouched(bool isPanRightTouched) { isPanRightTouched_ = isPanRightTouched; }
    bool GetIsPanRightTouched() { return isPanRightTouched_;  }
    
    virtual string GetName() { return "Navigator"; }
    virtual MediaTrack* GetTrack() { return nullptr; }
    virtual string GetChannelNumString() { return ""; }
    virtual bool GetIsChannelPinned() { return false; }
    virtual void IncBias() {}
    virtual void DecBias() {}
    virtual void PinChannel() {}
    virtual void UnpinChannel() {}
    virtual bool GetIsFocusedFXNavigator() { return false; }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int const channelNum_ = 0;
    int bias_ = 0;
    MediaTrack* pinnedTrack_ = nullptr;
    bool isChannelPinned_ = false;
    
protected:
    TrackNavigationManager* const manager_;

public:
    TrackNavigator(Page*  page, TrackNavigationManager* manager, int channelNum) : Navigator(page), manager_(manager), channelNum_(channelNum) {}
    TrackNavigator(Page*  page, TrackNavigationManager* manager) : Navigator(page), manager_(manager) {}
    virtual ~TrackNavigator() {}
    
    virtual bool GetIsChannelPinned() override { return isChannelPinned_; }
    virtual void IncBias() override { bias_++; }
    virtual void DecBias() override { bias_--; }
    
    virtual void PinChannel() override;
    virtual void UnpinChannel() override;
    
    virtual string GetName() override { return "TrackNavigator"; }
    
    virtual string GetChannelNumString() override { return to_string(channelNum_ + 1); }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MasterTrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    MasterTrackNavigator(Page*  page) : Navigator(page) {}
    virtual ~MasterTrackNavigator() {}
    
    virtual string GetName() override { return "MasterTrackNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectedTrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SelectedTrackNavigator(Page*  page) : Navigator(page) {}
    virtual ~SelectedTrackNavigator() {}
    
    virtual string GetName() override { return "SelectedTrackNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FocusedFXNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    FocusedFXNavigator(Page*  page) : Navigator(page) {}
    virtual ~FocusedFXNavigator() {}
    
    virtual bool GetIsFocusedFXNavigator() override { return true; }
    
    virtual string GetName() override { return "FocusedFXNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    Action* const action_ = nullptr;
    Widget* const widget_ = nullptr;
    Zone* const zone_ = nullptr;
    
    Widget* associatedWidget_ = nullptr;
    
    string lastStringValue_ = "";
    
    int intParam_ = 0;
    
    string stringParam_ = "";
    
    int paramIndex_ = 0;
    
    string fxParamDisplayName_ = "";
    
    int commandId_ = 0;
    
    double rangeMinimum_ = 0.0;
    double rangeMaximum_ = 1.0;
    
    vector<double> steppedValues_;
    int steppedValuesIndex_ = 0;
    
    double deltaValue_ = 0.0;
    vector<double> acceleratedDeltaValues_;
    vector<int> acceleratedTickValues_;
    int accumulatedIncTicks_ = 0;
    int accumulatedDecTicks_ = 0;
    
    bool isFeedbackInverted_ = false;
    double holdDelayAmount_ = 0.0;
    double delayStartTime_ = 0.0;
    double deferredValue_ = 0.0;
    
    bool shouldUseDisplayStyle_ = false;
    int displayStyle_ = 0;
    
    bool supportsRGB_ = false;
    vector<rgb_color> RGBValues_;
    int currentRGBIndex_ = 0;
    
    bool supportsTrackColor_ = false;
    
    vector<string> autoModeDisplayNames__ = { "Trim", "Read", "Touch", "Write", "Latch", "LtchPre" };
    int autoModeIndex_ = 0;
    
    vector<vector<string>> properties_;
    
public:
    ActionContext(Action* action, Widget* widget, Zone* zone, vector<string> params, vector<vector<string>> properties);
    virtual ~ActionContext() {}
    
    Widget* GetWidget() { return widget_; }
    Zone* GetZone() { return zone_; }
    int GetSlotIndex();
    string GetName();

    void SetAssociatedWidget(Widget* widget) { associatedWidget_ = widget; }
    Widget* GetAssociatedWidget() { return associatedWidget_; }

    int GetIntParam() { return intParam_; }
    string GetStringParam() { return stringParam_; }
    int GetCommandId() { return commandId_; }
    bool GetShouldUseDisplayStyle() { return shouldUseDisplayStyle_; }
    int GetDisplayStyle() { return displayStyle_; }
    
    MediaTrack* GetTrack();
    
    void DoRangeBoundAction(double value);
    void DoSteppedValueAction(double value);
    void DoAcceleratedSteppedValueAction(int accelerationIndex, double value);
    void DoAcceleratedDeltaValueAction(int accelerationIndex, double value);
    
    Page* GetPage();
    ControlSurface* GetSurface();
    TrackNavigationManager* GetTrackNavigationManager();
    MappedWidgetsNavigationManager* GetMappedWidgetsNavigationManager();
    int GetParamIndex() { return paramIndex_; }
    
    bool GetSupportsRGB() { return supportsRGB_; }
    
    void SetIsFeedbackInverted() { isFeedbackInverted_ = true; }
    void SetHoldDelayAmount(double holdDelayAmount) { holdDelayAmount_ = holdDelayAmount * 1000.0; } // holdDelayAmount is specified in seconds, holdDelayAmount_ is in milliseconds
    
    void DoAction(double value);
    void DoRelativeAction(double value);
    void DoRelativeAction(int accelerationIndex, double value);
    
    void RequestUpdate();
    void RunDeferredActions();
    void ClearWidget();
    void UpdateWidgetValue(double value);
    void UpdateWidgetValue(int param, double value);
    void UpdateWidgetValue(string value);
    void ForceWidgetValue(double value);

    void SetAutoModeIndex()
    {
        if(MediaTrack* track = GetTrack())
            autoModeIndex_ = DAW::GetMediaTrackInfo_Value(track, "I_AUTOMODE");
    }
    
    void NextAutoMode()
    {
        if(MediaTrack* track = GetTrack())
        {
            if(autoModeIndex_ == 2) // skip over write mode when cycling
                autoModeIndex_ += 2;
            else
                autoModeIndex_++;
            
            if(autoModeIndex_ > autoModeDisplayNames__.size() - 1)
                autoModeIndex_ = 0;
    
            DAW::GetSetMediaTrackInfo(track, "I_AUTOMODE", &autoModeIndex_);
        }
    }
    
    string GetAutoModeDisplayName()
    {
        int globalOverride = DAW::GetGlobalAutomationOverride();

        if(globalOverride > -1) // -1=no override, 0=trim/read, 1=read, 2=touch, 3=write, 4=latch, 5=bypass
            return autoModeDisplayNames__[globalOverride];
        else
            return autoModeDisplayNames__[autoModeIndex_];
    }
    
    void DoTouch(double value)
    {
        action_->Touch(this, value);
    }
    
    string GetFxParamDisplayName()
    {
        if(fxParamDisplayName_ != "")
            return fxParamDisplayName_;
        else if(MediaTrack* track = GetTrack())
        {
            char fxParamName[BUFSZ];
            DAW::TrackFX_GetParamName(track, GetSlotIndex(), paramIndex_, fxParamName, sizeof(fxParamName));
            return fxParamName;
        }
        
        return "";
    }

    void SetCurrentRGB(rgb_color newColor)
    {
        supportsRGB_ = true;
        RGBValues_[currentRGBIndex_] = newColor;
    }
    
    rgb_color GetCurrentRGB()
    {
        rgb_color blankColor;
        
        if(RGBValues_.size() > 0 && currentRGBIndex_ < RGBValues_.size())
            return RGBValues_[currentRGBIndex_];
        else return blankColor;
    }
    
    void SetSteppedValueIndex(double value)
    {
        int index = 0;
        double delta = 100000000.0;
        
        for(int i = 0; i < steppedValues_.size(); i++)
            if(abs(steppedValues_[i] - value) < delta)
            {
                delta = abs(steppedValues_[i] - value);
                index = i;
            }
        
        steppedValuesIndex_ = index;
    }

    string GetPanValueString(double panVal)
    {
        bool left = false;
        
        if(panVal < 0)
        {
            left = true;
            panVal = -panVal;
        }
        
        int panIntVal = int(panVal * 100.0);
        string trackPanValueString = "";
        
        if(left)
        {
            if(panIntVal == 100)
                trackPanValueString += "<";
            else if(panIntVal < 100 && panIntVal > 9)
                trackPanValueString += "< ";
            else
                trackPanValueString += "<  ";
            
            trackPanValueString += to_string(panIntVal);
        }
        else
        {
            trackPanValueString += "   ";
            
            trackPanValueString += to_string(panIntVal);
            
            if(panIntVal == 100)
                trackPanValueString += ">";
            else if(panIntVal < 100 && panIntVal > 9)
                trackPanValueString += " >";
            else
                trackPanValueString += "  >";
        }
        
        if(panIntVal == 0)
            trackPanValueString = "  <C>  ";

        return trackPanValueString;
    }
    
    string GetPanWidthValueString(double widthVal)
    {
        bool reversed = false;
        
        if(widthVal < 0)
        {
            reversed = true;
            widthVal = -widthVal;
        }
        
        int widthIntVal = int(widthVal * 100.0);
        string trackPanWidthString = "";
        
        if(reversed)
            trackPanWidthString += "Rev ";
        
        trackPanWidthString += to_string(widthIntVal);
        
        if(widthIntVal == 0)
            trackPanWidthString = " <Mno> ";

        return trackPanWidthString;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class WidgetContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    Widget* widget_ = nullptr;
    Zone* zone_ = nullptr;
    map<string, vector<ActionContext>> actionContextDictionary_;
    vector<ActionContext> defaultContexts_;
    
    vector<ActionContext>& GetActionContexts();

public:
    WidgetContext(Widget* widget, Zone* zone)  : widget_(widget), zone_(zone) {}
    
    void GetFormattedFXParamValue(char *buffer, int bufferSize);

    WidgetContext& operator=(WidgetContext &otherContext)
    {
        this->widget_ = otherContext.widget_;
        this->zone_ = otherContext.zone_;
        
        actionContextDictionary_.clear();
        
        for(auto [modifier, actionContexts] : otherContext.actionContextDictionary_)
            for(auto context : actionContexts)
                this->AddActionContext(modifier, context);
        
        return *this;
    }
    
    void AddActionContext(string modifier, ActionContext actionContext)
    {
        actionContextDictionary_[modifier].push_back(actionContext);
    }
    
    void RequestUpdate()
    {
        for(auto &context : GetActionContexts())
            context.RunDeferredActions();

        if(GetActionContexts().size() > 0)
        {
            ActionContext& context = GetActionContexts()[0];
            context.RequestUpdate();
        }
    }
    
    void DoAction(double value)
    {
        for(auto &context : GetActionContexts())
            context.DoAction(value);
    }
    
    void DoTouch(double value)
    {
        for(auto &context : GetActionContexts())
            context.DoTouch(value);
    }
    
    void DoRelativeAction(double delta)
    {
        for(auto &context : GetActionContexts())
            context.DoRelativeAction(delta);
    }
    
    void DoRelativeAction(int accelerationIndex, double delta)
    {
        for(auto &context : GetActionContexts())
            context.DoRelativeAction(accelerationIndex, delta);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Zone
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    ControlSurface* const surface_ = nullptr;
    Navigator* const navigator_= nullptr;
    string const name_ = "";
    string const alias_ = "";
    string const sourceFilePath_ = "";
    
    int slotIndex_ = 0;

    vector<Widget*> widgets_;

public:
    Zone(ControlSurface* surface, Navigator* navigator, string name, string alias, string sourceFilePath): surface_(surface), navigator_(navigator), name_(name), alias_(alias), sourceFilePath_(sourceFilePath) {}
    
    ControlSurface* GetSurface() { return surface_; }
    Navigator* GetNavigator() { return navigator_; }
    string GetPath() { return sourceFilePath_; }
    
    void SetSlotIndex(int index) { slotIndex_ = index; }
    int GetSlotIndex() { return slotIndex_; }
    
    void Deactivate();
    
    string GetName()
    {
        if(alias_ != "")
            return alias_;
        else
            return name_;
    }
    
    void AddWidget(Widget* widget)
    {
        widgets_.push_back(widget);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ActionTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string actionName;
    vector<string> params;
    vector<vector<string>> properties;
    bool isFeedbackInverted;
    double holdDelayAmount;
    
    ActionTemplate(string action, vector<string> prams, bool isInverted, double amount) : actionName(action), params(prams), isFeedbackInverted(isInverted), holdDelayAmount(amount) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ActionContextTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string modifier = "";
    vector<ActionTemplate*> members;
    
    ActionContextTemplate(string modifierStr) : modifier(modifierStr) {}
    
    ~ActionContextTemplate()
    {
        for(auto actionTemplate : members)
        {
            delete actionTemplate;
            actionTemplate = nullptr;
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct WidgetActionTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string widgetName = "";
    bool isModifier = false;
    vector<ActionContextTemplate*> actionContextTemplates;
    
    WidgetActionTemplate(string widgetNameStr) : widgetName(widgetNameStr) {}
    
    ~WidgetActionTemplate()
    {
        for(auto actionContextTemplate : actionContextTemplates)
        {
            delete actionContextTemplate;
            actionContextTemplate = nullptr;
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ZoneTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    vector<Navigator*> navigators;
    string name = "";
    string alias = "";
    string sourceFilePath = "";
    vector<string> includedZoneTemplates;
    vector<WidgetActionTemplate*> widgetActionTemplates;
    
    ZoneTemplate(vector<Navigator*> &navigatorList, string zoneName, string zoneAlias, string path, vector<string> includedZones, vector<WidgetActionTemplate*> &templates)
    : name(zoneName), alias(zoneAlias), sourceFilePath(path), includedZoneTemplates(includedZones)
    {
        for(auto navigator : navigatorList)
            navigators.push_back(navigator);

        for(auto widgetActionTemplate : templates)
            widgetActionTemplates.push_back(widgetActionTemplate);
    }
    
    ~ZoneTemplate()
    {
        for(auto widgetActionTemplate : widgetActionTemplates)
        {
            delete widgetActionTemplate;
            widgetActionTemplate = nullptr;
        }
    }
    
    void ProcessWidgetActionTemplates(ControlSurface* surface, Zone* zone, string channelNumStr, bool shouldUseNoAction);
    
    void  Activate(ControlSurface* surface, vector<Zone*> &activeZones);
    void  Activate(ControlSurface* surface, vector<Zone*> &activeZones, int maxNum);
    void  Activate(ControlSurface* surface, vector<Zone*> &activeZones, int slotindex, bool shouldUseNoAction);
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Widget
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    ControlSurface* const surface_;
    string const name_;
    vector<FeedbackProcessor*> feedbackProcessors_;
    
    bool isModifier_ = false;
    bool isToggled_ = false;
    
    WidgetContext currentWidgetContext_;
    WidgetContext defaultWidgetContext_;

    void LogInput(double value);
    
public:
    Widget(ControlSurface* surface, string name);
    ~Widget();
    
    ControlSurface* GetSurface() { return surface_; }
    string GetName() { return name_; }
    bool GetIsModifier() { return isModifier_; }
    void SetIsModifier() { isModifier_ = true; }
    void SilentSetValue(string displayText);
    
    void Toggle() { isToggled_ = ! isToggled_; }
    bool GetIsToggled() { return isToggled_; }
    
    void SetProperties(vector<vector<string>> properties);
    void UpdateValue(double value);
    void UpdateValue(int mode, double value);
    void UpdateValue(string value);
    void UpdateValueAndColors(string value, rgb_color textColor, rgb_color textBackground);
    void UpdateRGBValue(int r, int g, int b);
    void ForceValue(double value);
    void ForceValue(int mode, double value);
    void ForceValue(string value);
    void ForceValueAndColors(string value, rgb_color textColor, rgb_color textBackground);
    void ForceRGBValue(int r, int g, int b);
    void ClearCache();
    void Clear();
    void ForceClear();

    void GetFormattedFXParamValue(char *buffer, int bufferSize)
    {
        currentWidgetContext_.GetFormattedFXParamValue(buffer, bufferSize);
    }
    
    void RequestUpdate()
    {
        currentWidgetContext_.RequestUpdate();
    }
    
    void DoAction(double value)
    {
        LogInput(value);
        
        currentWidgetContext_.DoAction(value);
    }
    
    void DoRelativeAction(double delta)
    {
        LogInput(delta);
        
        currentWidgetContext_.DoRelativeAction(delta);
    }
    
    void DoRelativeAction(int accelerationIndex, double delta)
    {
        LogInput(accelerationIndex);
        
        currentWidgetContext_.DoRelativeAction(accelerationIndex, delta);
    }
   
    void DoTouch(double value)
    {
        LogInput(value);
        
        currentWidgetContext_.DoTouch(value);
    }
    
    void Activate(WidgetContext currentWidgetContext)
    {
        currentWidgetContext_ = currentWidgetContext;
    }
    
    void Deactivate()
    {
        currentWidgetContext_ = defaultWidgetContext_;
    }
    
    void MakeCurrentDefault()
    {
        defaultWidgetContext_ = currentWidgetContext_;
    }
    
    void AddFeedbackProcessor(FeedbackProcessor* feedbackProcessor)
    {
        feedbackProcessors_.push_back(feedbackProcessor);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Widget* const widget_;
    CSIMessageGenerator(Widget* widget) : widget_(widget) {}
    
public:
    CSIMessageGenerator(ControlSurface* surface, Widget* widget, string message);
    virtual ~CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value)
    {
        widget_->DoAction(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Touch_CSIMessageGenerator : CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    Touch_CSIMessageGenerator(ControlSurface* surface, Widget* widget, string message) : CSIMessageGenerator(surface, widget, message) {}
    virtual ~Touch_CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value) override
    {
        widget_->DoTouch(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Midi_CSIMessageGenerator(Widget* widget) : CSIMessageGenerator(widget) {}
    
public:
    virtual ~Midi_CSIMessageGenerator() {}
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    double lastDoubleValue_ = 0.0;
    string lastStringValue_ = "";

    Widget* const widget_ = nullptr;
    
public:
    FeedbackProcessor(Widget* widget) : widget_(widget) {}
    virtual ~FeedbackProcessor() {}
    Widget* GetWidget() { return widget_; }
    virtual void SetProperties(vector<vector<string>> properties) {}
    virtual void SetRGBValue(int r, int g, int b) {}
    virtual void ForceValue(double value) {}
    virtual void ForceValue(int param, double value) {}
    virtual void ForceRGBValue(int r, int g, int b) {}
    virtual void ForceValue(string value) {}
    virtual void SetValueAndColors(string value, rgb_color textColor, rgb_color textBackground) {}
    virtual void ForceValueAndColors(string value, rgb_color textColor, rgb_color textBackground) {}

    virtual void SetValue(double value)
    {
        if(lastDoubleValue_ != value)
            ForceValue(value);
    }
    
    virtual void SetValue(int param, double value)
    {
        if(lastDoubleValue_ != value)
            ForceValue(value);
    }
    
    virtual void SetValue(string value)
    {
        if(lastStringValue_ != value)
            ForceValue(value);
    }

    virtual void ClearCache()
    {
        lastDoubleValue_ = 0.0;
        lastStringValue_ = "";
    }
    
    virtual void Clear()
    {
        SetValue(0.0);
        SetValue(0, 0.0);
        SetValue("");
        SetRGBValue(0, 0, 0);
    }
    
    virtual void ForceClear()
    {
        ForceValue(0.0);
        ForceValue(0, 0.0);
        ForceValue("");
        ForceRGBValue(0, 0, 0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Midi_ControlSurface* const surface_ = nullptr;
    
    MIDI_event_ex_t* lastMessageSent_ = new MIDI_event_ex_t(0, 0, 0);
    MIDI_event_ex_t* midiFeedbackMessage1_ = new MIDI_event_ex_t(0, 0, 0);
    MIDI_event_ex_t* midiFeedbackMessage2_ = new MIDI_event_ex_t(0, 0, 0);
    
    Midi_FeedbackProcessor(Midi_ControlSurface* surface, Widget* widget) : FeedbackProcessor(widget), surface_(surface) {}
    Midi_FeedbackProcessor(Midi_ControlSurface* surface, Widget* widget, MIDI_event_ex_t* feedback1) : FeedbackProcessor(widget), surface_(surface), midiFeedbackMessage1_(feedback1) {}
    Midi_FeedbackProcessor(Midi_ControlSurface* surface, Widget* widget, MIDI_event_ex_t* feedback1, MIDI_event_ex_t* feedback2) : FeedbackProcessor(widget), surface_(surface), midiFeedbackMessage1_(feedback1), midiFeedbackMessage2_(feedback2) {}
    
    void SendMidiMessage(MIDI_event_ex_t* midiMessage);
    void SendMidiMessage(int first, int second, int third);
    void ForceMidiMessage(int first, int second, int third);

public:
    virtual void ClearCache() override
    {
        lastMessageSent_->midi_message[0] = 0;
        lastMessageSent_->midi_message[1] = 0;
        lastMessageSent_->midi_message[2] = 0;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    OSC_ControlSurface* const surface_ = nullptr;
    string oscAddress_ = "";
    
public:
    
    OSC_FeedbackProcessor(OSC_ControlSurface* surface, Widget* widget, string oscAddress) : FeedbackProcessor(widget), surface_(surface), oscAddress_(oscAddress) {}
    ~OSC_FeedbackProcessor() {}

    virtual void ForceValue(double value) override;
    virtual void ForceValue(int param, double value) override;
    virtual void ForceValue(string value) override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EuCon_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    EuCon_ControlSurface* const surface_ = nullptr;
    string address_ = "";
    
public:
    
    EuCon_FeedbackProcessor(EuCon_ControlSurface* surface, Widget* widget, string address) : FeedbackProcessor(widget), surface_(surface), address_(address) {}
    ~EuCon_FeedbackProcessor() {}

    virtual void ForceValue(double value) override;
    virtual void ForceValue(int param, double value) override;
    virtual void ForceValue(string value) override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EuCon_FeedbackProcessorDB : public EuCon_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    
    EuCon_FeedbackProcessorDB(EuCon_ControlSurface* surface, Widget* widget, string address) : EuCon_FeedbackProcessor(surface, widget, address) {}
    ~EuCon_FeedbackProcessorDB() {}
    
    virtual void Clear() override;
    virtual void ForceClear() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXActivationManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    ControlSurface* const surface_ = nullptr;
    int const numFXSlots_ = 0;
    vector<Zone*> activeSelectedTrackFXZones_;
    vector<Zone*> activeSelectedTrackFXMenuZones_;
    vector<Zone*> activeSelectedTrackFXMenuFXZones_;
    vector<Zone*> activeFocusedFXZones_;
    
public:
    FXActivationManager(ControlSurface* surface, int numFXSlots) : surface_(surface), numFXSlots_(numFXSlots) {}
    
    ~FXActivationManager()
    {
        for(auto zone : activeSelectedTrackFXZones_)
        {
            delete zone;
            zone = nullptr;
        }
        for(auto zone : activeSelectedTrackFXMenuZones_)
        {
            delete zone;
            zone = nullptr;
        }
        for(auto zone : activeSelectedTrackFXMenuFXZones_)
        {
            delete zone;
            zone = nullptr;
        }
        for(auto zone : activeFocusedFXZones_)
        {
            delete zone;
            zone = nullptr;
        }
    }
    
    int  GetNumFXSlots() { return numFXSlots_; }
    void MapSelectedTrackFXToWidgets();
    void MapSelectedTrackFXToMenu();
    void MapSelectedTrackFXSlotToWidgets(MediaTrack* selectedTrack, int slot);
    void MapFocusedFXToWidgets();
    void TrackFXListChanged();
    
    void DeactivateAllFXZones()
    {
        DeactivateSelectedTrackFXZones();
        DeactivateSelectedTrackFXMenuZones();
        DeactivateSelectedTrackFXMenuFXZones();
        DeactivateFocusedFXZones();
    }
    
    void DeactivateSelectedTrackFXZones()
    {
        for(auto zone : activeSelectedTrackFXZones_)
        {
            zone->Deactivate();
            delete zone;
        }
        
        activeSelectedTrackFXZones_.clear();
    }
    
    void DeactivateSelectedTrackFXMenuZones()
    {
        for(auto zone : activeSelectedTrackFXMenuZones_)
        {
            zone->Deactivate();
            delete zone;
        }
        
        activeSelectedTrackFXMenuZones_.clear();
    }
    
    void DeactivateSelectedTrackFXMenuFXZones()
    {
        for(auto zone : activeSelectedTrackFXMenuFXZones_)
        {
            zone->Deactivate();
            delete zone;
        }
        
        activeSelectedTrackFXMenuFXZones_.clear();
    }
    
    void DeactivateFocusedFXZones()
    {
        for(auto zone : activeFocusedFXZones_)
        {
            zone->Deactivate();
            delete zone;
        }
        
        activeFocusedFXZones_.clear();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset);

    CSurfIntegrator* const CSurfIntegrator_ ;
    Page* const page_;
    string const name_;
    Zone* const defaultZone_ = nullptr;

    map<string, CSIMessageGenerator*> CSIMessageGeneratorsByMessage_;
    
    string zoneFolder_ = "";
    int numChannels_ = 0;
    int numSends_ = 0;
    int options_ = 0;
    
    map<int, Navigator*> navigators_;
    
    vector<Widget*> widgets_;
    map<string, Widget*> widgetsByName_;

    vector<Zone*> activeZones_;

    vector<Zone*> activeSendZones_;
    
    FXActivationManager* const fxActivationManager_;

    virtual void SurfaceOutMonitor(Widget* widget, string address, string value);

    void InitZones(string zoneFolder);

    map<string, ZoneTemplate*> zoneTemplates_;
    
    virtual void InitHardwiredWidgets()
    {
        // Add the "hardwired" widgets
        AddWidget(new Widget(this, "OnTrackSelection"));
        AddWidget(new Widget(this, "OnFXFocus"));
        AddWidget(new Widget(this, "OnPageEnter"));
        AddWidget(new Widget(this, "OnPageLeave"));
        AddWidget(new Widget(this, "OnInitialization"));
    }
    
public:
    virtual ~ControlSurface()
    {
        delete defaultZone_;
        delete fxActivationManager_;

        for(auto [key, messageGenerator] : CSIMessageGeneratorsByMessage_)
        {
            delete messageGenerator;
            messageGenerator = nullptr;
        }
        
        for(auto widget : widgets_)
        {
            delete widget;
            widget = nullptr;
        }

        for(auto zone : activeZones_)
        {
            delete zone;
            zone = nullptr;
        }
        
        for(auto zone: activeSendZones_)
        {
            delete zone;
            zone = nullptr;
        }
        
        for(auto [key, zoneTemplate] : zoneTemplates_)
        {
            delete zoneTemplate;
            zoneTemplate = nullptr;
        }
    };
    
    Page* GetPage() { return page_; }
    string GetName() { return name_; }
    
    virtual string GetSourceFileName() { return ""; }
    vector<Widget*> &GetWidgets() { return widgets_; }
    
    int GetNumChannels() { return numChannels_; }
    int GetNumSends() { return numSends_; }

    Navigator* GetNavigatorForChannel(int channelNum);
    
    FXActivationManager* GetFXActivationManager() { return fxActivationManager_; }
    Zone* GetDefaultZone() { return defaultZone_; }

    virtual void SetHasMCUMeters(int displayType) {}
    
    virtual void LoadingZone(string zoneName) {}
    virtual void HandleExternalInput() {}
    virtual void InitializeEuCon() {}
    virtual void UpdateTimeDisplay() {}
    void MapSelectedTrackSendsToWidgets();
    
    virtual bool GetIsEuConFXAreaFocused() { return false; }

    virtual void ForceRefreshTimeDisplay() {}
   
    void MakeHomeDefault()
    {
        if(zoneTemplates_.count("Home") > 0)
        {
            vector<Zone*> dummyZones; //  We don't want to put "Home" in any active Zones list - it is the default Zone and has Control Surface lifetime
            
            zoneTemplates_["Home"]->Activate(this, dummyZones);
        
            for(auto widget : widgets_)
                widget->MakeCurrentDefault();
        }
    }
    
    void GoZone(string zoneName)
    {
        if(zoneTemplates_.count(zoneName) > 0)
        {
            if(zoneName == "Home")
            {
                DeactivateActiveZones();
                DeactivateSendsZones();
                fxActivationManager_->DeactivateAllFXZones();
            }
            else
            {
                zoneTemplates_[zoneName]->Activate(this, activeZones_);
            }
        }
    }
    
    void DeactivateSendsZones()
    {
        for(auto zone : activeSendZones_)
        {
            zone->Deactivate();
            delete zone;
        }
        
        activeSendZones_.clear();
    }

    void DeactivateActiveZones()
    {
        for(auto zone : activeZones_)
        {
            zone->Deactivate();
            delete zone;
        }
        
        activeZones_.clear();
    }

    ZoneTemplate* GetZoneTemplate(string zoneName)
    {
        if(zoneTemplates_.count(zoneName) > 0)
            return zoneTemplates_[zoneName];
        else
            return nullptr;
    }
   
    void AddZoneTemplate(ZoneTemplate* zoneTemplate)
    {
        if(zoneTemplates_.count(zoneTemplate->name) > 0)
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "%s already has a Zone named: %s -- please check for duplicate zone defintions.\n", name_.c_str(), zoneTemplate->name.c_str());
            DAW::ShowConsoleMsg(buffer);
        }
        else
            zoneTemplates_[zoneTemplate->name] = zoneTemplate;
    }

    virtual void RequestUpdate()
    {
        for(auto widget : widgets_)
            widget->RequestUpdate();
    }

    virtual void ForceClearAllWidgets()
    {
        for(auto widget : widgets_)
            widget->ForceClear();
    }
    
    void ClearCache()
    {
        for(auto widget : widgets_)
        {
            widget->UpdateValue(0.0);
            widget->UpdateValue(0, 0.0);
            widget->UpdateValue("");
            widget->ClearCache();
        }
    }
    
    void AddWidget(Widget* widget)
    {
        widgets_.push_back(widget);
        widgetsByName_[widget->GetName()] = widget;
    }
    
    void AddCSIMessageGenerator(string message, CSIMessageGenerator* messageGenerator)
    {
        CSIMessageGeneratorsByMessage_[message] = messageGenerator;
    }

    Widget* GetWidgetByName(string name)
    {
        if(widgetsByName_.count(name) > 0)
            return widgetsByName_[name];
        else
            return nullptr;
    }
    
    void OnTrackSelection()
    {
        if(widgetsByName_.count("OnTrackSelection") > 0)
            widgetsByName_["OnTrackSelection"]->DoAction(1.0);
    }
    
    void OnFXFocus(MediaTrack* track, int fxIndex)
    {
        if(widgetsByName_.count("OnFXFocus") > 0)
            widgetsByName_["OnFXFocus"]->DoAction(1.0);
    }
    
    void OnPageEnter()
    {
        if(widgetsByName_.count("OnPageEnter") > 0)
            widgetsByName_["OnPageEnter"]->DoAction(1.0);
    }
    
    void OnPageLeave()
    {
        if(widgetsByName_.count("OnPageLeave") > 0)
            widgetsByName_["OnPageLeave"]->DoAction(1.0);
    }
    
    void OnInitialization()
    {
        if(widgetsByName_.count("OnInitialization") > 0)
            widgetsByName_["OnInitialization"]->DoAction(1.0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string templateFilename_ = "";
    midi_Input* midiInput_ = nullptr;
    midi_Output* midiOutput_ = nullptr;
    map<int, vector<Midi_CSIMessageGenerator*>> Midi_CSIMessageGeneratorsByMessage_;
    
    // special processing for MCU meters
    bool hasMCUMeters_ = false;
    int displayType_ = 0x14;
    
    void ProcessMidiMessage(const MIDI_event_ex_t* evt);
   
    void InitWidgets(string templateFilename, string zoneFolder);

    void InitializeMCU();
    void InitializeMCUXT();
    
    virtual void Initialize()
    {
        if(hasMCUMeters_)
        {
            if(displayType_ == 0x14)
                InitializeMCU();
            else
                InitializeMCUXT();
        }
    }

public:
    Midi_ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string templateFilename, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset, midi_Input* midiInput, midi_Output* midiOutput)
    : ControlSurface(CSurfIntegrator, page, name, zoneFolder, numChannels, numSends, numFX, channelOffset), templateFilename_(templateFilename), midiInput_(midiInput), midiOutput_(midiOutput)
    {
        InitWidgets(templateFilename, zoneFolder);
    }
    
    virtual ~Midi_ControlSurface() {}
    
    virtual string GetSourceFileName() override { return "/CSI/Surfaces/Midi/" + templateFilename_; }
    
    void SendMidiMessage(MIDI_event_ex_t* midiMessage);
    void SendMidiMessage(int first, int second, int third);

    virtual void SetHasMCUMeters(int displayType) override
    {
        hasMCUMeters_ = true;
        displayType_ = displayType;
    }
    
    virtual void HandleExternalInput() override
    {
        if(midiInput_)
        {
            DAW::SwapBufsPrecise(midiInput_);
            MIDI_eventlist* list = midiInput_->GetReadBuf();
            int bpos = 0;
            MIDI_event_t* evt;
            while ((evt = list->EnumItems(&bpos)))
                ProcessMidiMessage((MIDI_event_ex_t*)evt);
        }
    }
    
    void AddCSIMessageGenerator(int message, Midi_CSIMessageGenerator* messageGenerator)
    {
        Midi_CSIMessageGeneratorsByMessage_[message].push_back(messageGenerator);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string templateFilename_ = "";
    oscpkt::UdpSocket* const inSocket_ = nullptr;
    oscpkt::UdpSocket* const outSocket_ = nullptr;
    oscpkt::PacketReader packetReader_;
    oscpkt::PacketWriter packetWriter_;
    
    void InitWidgets(string templateFilename, string zoneFolder);
    void ProcessOSCMessage(string message, double value);

public:
    OSC_ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string templateFilename, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset, oscpkt::UdpSocket* inSocket, oscpkt::UdpSocket* outSocket)
    : ControlSurface(CSurfIntegrator, page, name, zoneFolder, numChannels, numSends, numFX, channelOffset), templateFilename_(templateFilename), inSocket_(inSocket), outSocket_(outSocket)
    {
        InitWidgets(templateFilename, zoneFolder);
    }
    
    virtual ~OSC_ControlSurface() {}
    
    virtual string GetSourceFileName() override { return "/CSI/Surfaces/OSC/" + templateFilename_; }
    
    virtual void LoadingZone(string zoneName) override;
    void SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, double value);
    void SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, string value);
    
    virtual void ForceClearAllWidgets() override
    {
        LoadingZone("Home");
        ControlSurface::ForceClearAllWidgets();
    }
    
    virtual void HandleExternalInput() override
    {
        if(inSocket_ != nullptr && inSocket_->isOk())
        {
            while (inSocket_->receiveNextPacket(0))  // timeout, in ms
            {
                packetReader_.init(inSocket_->packetData(), inSocket_->packetSize());
                oscpkt::Message *message;
                
                while (packetReader_.isOk() && (message = packetReader_.popMessage()) != 0)
                {
                    float value = 0;
                    
                    if(message->arg().isFloat())
                    {
                        message->arg().popFloat(value);
                        ProcessOSCMessage(message->addressPattern(), value);
                    }
                }
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// For EuCon_ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class WidgetGroup
{
private:
    bool isVisible_ = false;
    
    vector<Widget*> widgets_;
    map<string, WidgetGroup*> subGroups_;
    
public:
    ~WidgetGroup()
    {
        for(auto [key, group] : subGroups_)
        {
            delete group;
            group = nullptr;
        }
    }
    
    void SetIsVisible(bool isVisible)
    {
        isVisible_ = isVisible;
    }
    
    void SetIsVisible(string subgroupName, bool isVisible)
    {
        if(subGroups_.count(subgroupName) > 0)
           subGroups_[subgroupName]->SetIsVisible(isVisible);
    }
    
    void RequestUpdate()
    {
        if(isVisible_)
        {
            for(auto widget : widgets_)
                widget->RequestUpdate();
            
            for(auto [name, group] : subGroups_)
                group->RequestUpdate();
        }
    }
    
    void AddWidget(Widget* widget)
    {
        widgets_.push_back(widget);
    }
    
    void AddWidgetToSubgroup(string subgroupName, Widget* widget)
    {
        if(subGroups_.count(subgroupName) < 1)
            subGroups_[subgroupName] = new WidgetGroup();
        
        subGroups_[subgroupName]->AddWidget(widget);
    }
};

typedef struct PeakInfoStruct
{
    double timePeakSet;
    float peakValue;
    bool isClipping;
} PeakInfo;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EuCon_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    bool isEuConFXAreaFocused_ = false;
    double previousPP = 0.0;
    
    vector<Widget*> generalWidgets_;
    map<int, WidgetGroup*> channelGroups_;
    
    Widget* InitializeEuConWidget(CSIWidgetInfo &widgetInfo);
    
    map<int, PeakInfo> peakInfo_;
    
protected:
    virtual void InitHardwiredWidgets() override
    {
        ControlSurface::InitHardwiredWidgets();
        // Add the "hardwired" widgets
        AddWidget(new Widget(this, "OnEuConFXAreaGainedFocus"));
        AddWidget(new Widget(this, "OnEuConFXAreaLostFocus"));
    }
    
public:
    EuCon_ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset);
    
    virtual ~EuCon_ControlSurface()
    {
        for(auto [key, group] : channelGroups_)
        {
            delete group;
            group = nullptr;
        }
    }
    
    virtual string GetSourceFileName() override { return "EuCon"; }
    
    virtual bool GetIsEuConFXAreaFocused() override { return isEuConFXAreaFocused_; }

    virtual void InitializeEuCon() override;
    virtual void InitializeEuConWidgets(vector<CSIWidgetInfo> *widgetInfoItems);
    void SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, double value);
    void SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, double value, int param);
    void SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, string value);
    void SendEuConMessage(string address, string value);
    void HandleEuConMessage(string address, double value);
    void HandleEuConGroupVisibilityChange(string groupName, int channelNumber, bool isVisible);
    void HandleEuConGetMeterValues(int id, int iLeg, float* oLevel, float* oPeak, bool* oLegClip);
    void HandleEuConGetFormattedFXParamValue(const char* address, char *buffer, int bufferSize);
    virtual void UpdateTimeDisplay() override;
    
    virtual void RequestUpdate() override
    {
        for(auto widget : generalWidgets_)
            widget->RequestUpdate();
        
        for(auto [channel, group] : channelGroups_)
            group->RequestUpdate();
        
        SendEuConMessage("RequestUpdateMeters", "Update");
    }

    virtual void ForceRefreshTimeDisplay() override
    {
        previousPP = 0.5;
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MappedWidgetsNavigationManager
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    Page* const page_ = nullptr;
    int sendsMappingNum_ = 0;
    int fxMenuMappingNum_ = 0;

    int mappedSendsOffset_ = 0;
    int mappedFxMenuOffset_ = 0;

    MediaTrack* previousSelectedTrack = nullptr;
public:
    MappedWidgetsNavigationManager(Page* page, int sendsMappingNum, int fxMenuMappingNum) : page_(page), sendsMappingNum_(sendsMappingNum), fxMenuMappingNum_(fxMenuMappingNum) {}

    void AdjustMappedWidgetsBank(int amount);
    
    void OnTrackSelection() 
    {
        ResetMappedWidgetsBankIfTrackChanged();
    }
    void OnTrackListChange()
    {
        ResetMappedWidgetsBankIfTrackChanged();
    }
    void OnTrackSelectionBySurface(MediaTrack* track)
    {
        ResetMappedWidgetsBankIfTrackChanged();
    }
    void ResetMappedWidgetsBankIfTrackChanged();
    void ResetMappedWidgetsBank();
    int GetMappedSendsOffset() { return mappedSendsOffset_; }
    int GetMappedFxMenuOffset() { return mappedFxMenuOffset_; }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNavigationManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    Page* const page_ = nullptr;
    bool followMCP_ = true;
    bool synchPages_ = false;
    bool scrollLink_ = false;
    bool vcaMode_ = false;
    int targetScrollLinkChannel_ = 0;
    int trackOffset_ = 0;
    int savedTrackOffset_ = 0;
    int savedVCAOffset_ = 0;
    vector<MediaTrack*> tracks_;
    vector<MediaTrack*> vcaSpillTracks_;
    vector<Navigator*> navigators_;
    Navigator* const masterTrackNavigator_ = nullptr;
    Navigator* const selectedTrackNavigator_ = nullptr;
    Navigator* const focusedFXNavigator_ = nullptr;
    Navigator* const defaultNavigator_ = nullptr;
public:
    TrackNavigationManager(Page* page, bool followMCP, bool synchPages, bool scrollLink, int numChannels) : page_(page), followMCP_(followMCP), synchPages_(synchPages), scrollLink_(scrollLink),
    masterTrackNavigator_(new MasterTrackNavigator(page_)), selectedTrackNavigator_(new SelectedTrackNavigator(page_)), focusedFXNavigator_(new FocusedFXNavigator(page_)), defaultNavigator_(new Navigator(page_))
    {
        for(int i = 0; i < numChannels; i++)
            navigators_.push_back(new TrackNavigator(page_, this, i));
    }
    
    ~TrackNavigationManager()
    {
        for(auto navigator : navigators_)
        {
            delete navigator;
            navigator = nullptr;
        }
        
        delete masterTrackNavigator_;
        delete selectedTrackNavigator_;
        delete focusedFXNavigator_;
        delete defaultNavigator_;
    }
    
    Page* GetPage() { return page_; }
    bool GetSynchPages() { return synchPages_; }
    bool GetScrollLink() { return scrollLink_; }
    bool GetVCAMode() { return vcaMode_; }
    int  GetNumTracks() { return DAW::CSurf_NumTracks(followMCP_); }
    Navigator* GetMasterTrackNavigator() { return masterTrackNavigator_; }
    Navigator* GetSelectedTrackNavigator() { return selectedTrackNavigator_; }
    Navigator* GetFocusedFXNavigator() { return focusedFXNavigator_; }
    Navigator* GetDefaultNavigator() { return defaultNavigator_; }

    void ForceScrollLink();
    void OnTrackSelectionBySurface(MediaTrack* track);
    void AdjustTrackBank(int amount);

    void IncChannelBias(MediaTrack* track, int channelNum)
    {
        for(int i = channelNum + 1; i < navigators_.size(); i++)
            navigators_[i]->IncBias();
    }
    
    void DecChannelBias(MediaTrack* track, int channelNum)
    {
        for(int i = channelNum + 1; i < navigators_.size(); i++)
            navigators_[i]->DecBias();
    }
    
    void TogglePin(MediaTrack* track)
    {
        for(auto navigator : navigators_)
        {
            if(track == navigator->GetTrack())
            {
                if(navigator->GetIsChannelPinned())
                    navigator->UnpinChannel();
                else
                    navigator->PinChannel();
                
                break;
            }
        }
    }
    
    void ToggleVCAMode()
    {
        if(vcaMode_)
        {
            savedVCAOffset_ = trackOffset_;
            trackOffset_ = savedTrackOffset_;
            vcaMode_ = false;
        }
        else
        {
            savedTrackOffset_ = trackOffset_;
            trackOffset_ = savedVCAOffset_;
            vcaMode_ = true;
        }
    }
 
    Navigator* GetNavigatorForChannel(int channelNum)
    {
        if(channelNum < navigators_.size())
            return navigators_[channelNum];
        else
            return nullptr;
    }
    
    MediaTrack* GetTrackFromChannel(int channelNumber)
    {
        int trackNumber = channelNumber + trackOffset_;
        
        if(tracks_.size() > trackNumber && DAW::ValidateTrackPtr(tracks_[trackNumber]))
            return tracks_[trackNumber];
        else
            return nullptr;
    }
    
    MediaTrack* GetTrackFromId(int trackNumber)
    {
        if(trackNumber <= GetNumTracks())
            return DAW::CSurf_TrackFromID(trackNumber, followMCP_);
        else
            return nullptr;
    }

    void OnTrackSelection()
    {
        if(scrollLink_)
            ForceScrollLink();
    }
    
    void OnTrackListChange()
    {
        if(scrollLink_)
            ForceScrollLink();
    }

    void ToggleVCASpill(MediaTrack* track)
    {
        if(find(vcaSpillTracks_.begin(), vcaSpillTracks_.end(), track) == vcaSpillTracks_.end())
            vcaSpillTracks_.push_back(track);
        else
            vcaSpillTracks_.erase(find(vcaSpillTracks_.begin(), vcaSpillTracks_.end(), track));
    }
    
    // For vcaSpillTracks_.erase -- see Clean up vcaSpillTracks below
    static bool IsTrackPointerStale(MediaTrack* track)
    {
        return ! DAW::ValidateTrackPtr(track);
    }
    
    void RebuildTrackList()
    {
        int top = GetNumTracks() - navigators_.size();
        
        if(top < 0)
            trackOffset_ = 0;
        else if(trackOffset_ >  top)
            trackOffset_ = top;

        tracks_.clear();
        
        // Clean up vcaSpillTracks
        vcaSpillTracks_.erase(remove_if(vcaSpillTracks_.begin(), vcaSpillTracks_.end(), IsTrackPointerStale), vcaSpillTracks_.end());

        // Get Visible Tracks
        for (int i = 1; i <= GetNumTracks(); i++)
        {
            MediaTrack* track = DAW::CSurf_TrackFromID(i, followMCP_);
            
            if(DAW::IsTrackVisible(track, followMCP_))
            {
                if(vcaMode_)
                {
                    int vcaMasterGroup = DAW::GetSetTrackGroupMembership(track, "VOLUME_VCA_MASTER", 0, 0);

                    if(vcaMasterGroup != 0 && DAW::GetSetTrackGroupMembership(track, "VOLUME_VCA_SLAVE", 0, 0) == 0) // Only top level Masters for now
                    {
                        tracks_.push_back(track);
                        
                        if(find(vcaSpillTracks_.begin(), vcaSpillTracks_.end(), track) != vcaSpillTracks_.end()) // should spill slaves for this master
                            for (int j = 1; j <= GetNumTracks(); j++)
                                if(vcaMasterGroup == DAW::GetSetTrackGroupMembership(DAW::CSurf_TrackFromID(j, followMCP_), "VOLUME_VCA_SLAVE", 0, 0)) // if this track is slave of master
                                    tracks_.push_back(DAW::CSurf_TrackFromID(j, followMCP_));
                    }
                }
                else
                    tracks_.push_back(track);
            }
        }

        for(auto navigator : navigators_)
        {
            if(navigator->GetIsChannelPinned())
            {
                if(DAW::ValidateTrackPtr(navigator->GetTrack()))
                    remove(tracks_.begin(), tracks_.end(), navigator->GetTrack());
                else
                    navigator->UnpinChannel();
            }
        }
    }
    
    bool GetIsControlTouched(MediaTrack* track, int touchedControl)
    {
        if(track == GetMasterTrackNavigator()->GetTrack())
            return GetIsNavigatorTouched(GetMasterTrackNavigator(), touchedControl);
        
        for(auto navigator : navigators_)
            if(track == navigator->GetTrack())
                return GetIsNavigatorTouched(navigator, touchedControl);
 
        if(MediaTrack* selectedTrack = GetSelectedTrack())
             if(track == selectedTrack)
                return GetIsNavigatorTouched(GetSelectedTrackNavigator(), touchedControl);
        
        if(MediaTrack* focusedFXTrack = GetFocusedFXNavigator()->GetTrack())
            if(track == focusedFXTrack)
                return GetIsNavigatorTouched(GetFocusedFXNavigator(), touchedControl);

        return false;
    }
    
    bool GetIsNavigatorTouched(Navigator* navigator,  int touchedControl)
    {
        if(touchedControl == 0)
            return navigator->GetIsVolumeTouched();
        else if(touchedControl == 1)
        {
            if(navigator->GetIsPanTouched() || navigator->GetIsPanLeftTouched())
                return true;
        }
        else if(touchedControl == 2)
        {
            if(navigator->GetIsPanWidthTouched() || navigator->GetIsPanRightTouched())
                return true;
        }

        return false;
    }
    
    void EnterPage()
    {
        /*
         if(colourTracks_)
         {
         // capture track colors
         for(auto* navigator : trackNavigators_)
         if(MediaTrack* track = DAW::GetTrackFromGUID(navigator->GetTrackGUID(), followMCP_))
         trackColours_[navigator->GetTrackGUID()] = DAW::GetTrackColor(track);
         }
         */
    }
    
    void LeavePage()
    {
        /*
         if(colourTracks_)
         {
         DAW::PreventUIRefresh(1);
         // reset track colors
         for(auto* navigator : trackNavigators_)
         if(MediaTrack* track = DAW::GetTrackFromGUID(navigator->GetTrackGUID(), followMCP_))
         if(trackColours_.count(navigator->GetTrackGUID()) > 0)
         DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", &trackColours_[navigator->GetTrackGUID()]);
         DAW::PreventUIRefresh(-1);
         }
         */
    }
    
    void ToggleScrollLink(int targetChannel)
    {
        targetScrollLinkChannel_ = targetChannel - 1 < 0 ? 0 : targetChannel - 1;
        
        scrollLink_ = ! scrollLink_;
        
        OnTrackSelection();
    }
    
    MediaTrack* GetSelectedTrack()
    {
        if(DAW::CountSelectedTracks(NULL) != 1)
            return nullptr;
        
        MediaTrack* track = nullptr;
        
        for(int i = 0; i <= GetNumTracks(); i++)
        {
            if(DAW::GetMediaTrackInfo_Value(GetTrackFromId(i), "I_SELECTED"))
            {
                track = GetTrackFromId(i);
                break;
            }
        }
        
        return track;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Page
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string name_ = "";
    vector<ControlSurface*> surfaces_;
    
    bool isShift_ = false;
    double shiftPressedTime_ = 0;
    bool isOption_ = false;
    double optionPressedTime_ = 0;
    bool isControl_ = false;
    double controlPressedTime_ = 0;
    bool isAlt_ = false;
    double altPressedTime_ = 0;
    
    TrackNavigationManager* const trackNavigationManager_ = nullptr;
    MappedWidgetsNavigationManager* const mappedWidgetsNavigationManager_ = nullptr;

    Navigator* defaultNavigator_ = nullptr;
    
public:
    Page(string name, bool followMCP, bool synchPages, bool scrollLink, int numChannels, int numSends, int numFX) : name_(name),  trackNavigationManager_(new TrackNavigationManager(this, followMCP, synchPages, scrollLink, numChannels)), 
        mappedWidgetsNavigationManager_(new MappedWidgetsNavigationManager(this, numSends, numFX)),
        defaultNavigator_(new Navigator(this)) { }
    
    ~Page()
    {
        for(auto surface: surfaces_)
        {
            delete surface;
            surface = nullptr;
        }
        
        delete trackNavigationManager_;
        delete defaultNavigator_;
        delete mappedWidgetsNavigationManager_;
    }
    
    string GetName() { return name_; }
    TrackNavigationManager* GetTrackNavigationManager() { return trackNavigationManager_; }
    MappedWidgetsNavigationManager* GetMappedWidgetsNavigationManager() { return mappedWidgetsNavigationManager_; }
    
    bool GetShift() { return isShift_; }
    bool GetOption() { return isOption_; }
    bool GetControl() { return isControl_; }
    bool GetAlt() { return isAlt_; }

    Navigator* GetDefaultNavigator() { return defaultNavigator_; }
    
    void InitializeEuCon()
    {
        for(auto surface : surfaces_)
            surface->InitializeEuCon();
    }
    
    /*
    int repeats = 0;
    
    void Run()
    {
        int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        trackNavigationManager_->RebuildTrackList();
        
        for(auto surface : surfaces_)
            surface->HandleExternalInput();
        
        for(auto surface : surfaces_)
            surface->RequestUpdate();
        
         repeats++;
         
         if(repeats > 50)
         {
             repeats = 0;
             
             int totalDuration = 0;

             start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
             trackNavigationManager_->RebuildTrackList();
             int duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
             totalDuration += duration;
             ShowDuration("Rebuild Track List", duration);
             
             for(auto surface : surfaces_)
             {
                 start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                 surface->HandleExternalInput();
                 duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
                 totalDuration += duration;
                 ShowDuration(surface->GetName(), "HandleExternalInput", duration);
             }
             
             for(auto surface : surfaces_)
             {
                 start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                 surface->RequestUpdate();
                 duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
                 totalDuration += duration;
                 ShowDuration(surface->GetName(), "Request Update", duration);
             }
             
             char msgBuffer[250];
             
             sprintf(msgBuffer, "Total duration = %d\n\n\n", totalDuration);
             DAW::ShowConsoleMsg(msgBuffer);
         }
    }
    
    
    void ShowDuration(string item, int duration)
    {
        char msgBuffer[250];
        
        sprintf(msgBuffer, "%s - %d microseconds\n", item.c_str(), duration);
        DAW::ShowConsoleMsg(msgBuffer);
    }
    
    void ShowDuration(string surface, string item, int duration)
    {
        char msgBuffer[250];
        
        sprintf(msgBuffer, "%s - %s - %d microseconds\n", surface.c_str(), item.c_str(), duration);
        DAW::ShowConsoleMsg(msgBuffer);
    }
    */


    void Run()
    {
        trackNavigationManager_->RebuildTrackList();
        
        for(auto surface : surfaces_)
            surface->HandleExternalInput();
        
        for(auto surface : surfaces_)
            surface->RequestUpdate();
    }

    void ForceClearAllWidgets()
    {
        for(auto surface : surfaces_)
            surface->ForceClearAllWidgets();
    }
    
    void ForceRefreshTimeDisplay()
    {
        for(auto surface : surfaces_)
            surface->ForceRefreshTimeDisplay();
    }

    void AddSurface(ControlSurface* surface)
    {
        surfaces_.push_back(surface);
    }
    
    bool GetTouchState(MediaTrack* track, int touchedControl)
    {
        return trackNavigationManager_->GetIsControlTouched(track, touchedControl);
    }
    
    void SetShift(bool value)
    {
        SetModifier(value, isShift_, shiftPressedTime_);
    }
 
    void SetOption(bool value)
    {
        SetModifier(value, isOption_, optionPressedTime_);
    }
    
    void SetControl(bool value)
    {
        SetModifier(value, isControl_, controlPressedTime_);
    }
    
    void SetAlt(bool value)
    {
        SetModifier(value, isAlt_, altPressedTime_);
    }
  
    void SetModifier(bool value, bool &modifier, double &modifierPressedTime)
    {
        if(value && modifier == false)
        {
            modifier = value;
            modifierPressedTime = DAW::GetCurrentNumberOfMilliseconds();
        }
        else
        {
            double keyReleasedTime = DAW::GetCurrentNumberOfMilliseconds();
            
            if(keyReleasedTime - modifierPressedTime > 100)
            {
                modifier = value;
            }
        }
    }

    string GetModifier()
    {
        string modifier = "";
        
        if(isShift_)
            modifier += Shift + "+";
        if(isOption_)
            modifier += Option + "+";
        if(isControl_)
            modifier +=  Control + "+";
        if(isAlt_)
            modifier += Alt + "+";
        
        return modifier;
    }
    
    void OnTrackSelection()
    {
        trackNavigationManager_->OnTrackSelection();
        mappedWidgetsNavigationManager_->OnTrackSelection();
        
        for(auto surface : surfaces_)
            surface->OnTrackSelection();
    }
    
    void OnTrackListChange()
    {
        trackNavigationManager_->OnTrackListChange();
        mappedWidgetsNavigationManager_->OnTrackListChange();
    }
    
    void OnTrackSelectionBySurface(MediaTrack* track)
    {
        trackNavigationManager_->OnTrackSelectionBySurface(track);
        mappedWidgetsNavigationManager_->OnTrackSelectionBySurface(track);
       
        for(auto surface : surfaces_)
            surface->OnTrackSelection();
    }
    
    void OnFXFocus(MediaTrack *track, int fxIndex)
    {
        for(auto surface : surfaces_)
            surface->OnFXFocus(track, fxIndex);
    }

    void TrackFXListChanged(MediaTrack* track)
    {
        for(auto surface : surfaces_)
            surface->GetFXActivationManager()->TrackFXListChanged();
    }

    void EnterPage()
    {
        trackNavigationManager_->EnterPage();
        
        for(auto surface : surfaces_)
            surface->ClearCache();
        
        for(auto surface : surfaces_)
            surface->OnPageEnter();
    }
    
    void LeavePage()
    {
        trackNavigationManager_->LeavePage();
        
        for(auto surface : surfaces_)
            surface->OnPageLeave();

        for(auto surface : surfaces_)
            surface->ClearCache();        
    }
    
    void OnInitialization()
    {
        for(auto surface : surfaces_)
            surface->OnInitialization();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    CSurfIntegrator* const CSurfIntegrator_ = nullptr;

    map<string, Action*> actions_;

    vector <Page*> pages_;
    
    map<string, map<string, int>> fxParamIndices_;
    
    int currentPageIndex_ = 0;
    bool surfaceInDisplay_ = false;
    bool surfaceOutDisplay_ = false;
    bool fxParamsDisplay_ = false;
    bool fxParamsWrite_ = false;

    bool shouldRun_ = true;
    
    int *timeModePtr_ = nullptr;
    int *timeMode2Ptr_ = nullptr;
    int *measOffsPtr_ = nullptr;
    double *timeOffsPtr_ = nullptr;
    int *projectPanModePtr_ = nullptr;
    
    void InitActionsDictionary();

    double GetPrivateProfileDouble(string key)
    {
        char tmp[512];
        memset(tmp, 0, sizeof(tmp));
        
        DAW::GetPrivateProfileString("REAPER", key.c_str() , "", tmp, sizeof(tmp), DAW::get_ini_file());
        
        return strtod (tmp, NULL);
    }
    
public:
    ~Manager()
    {
        for(auto page : pages_)
        {
            delete page;
            page = nullptr;
        }
        
        for(auto [key, action] : actions_)
        {
            delete action;
            action = nullptr;
        }
    }
    
    Manager(CSurfIntegrator* CSurfIntegrator) : CSurfIntegrator_(CSurfIntegrator)
    {
        InitActionsDictionary();

        int size = 0;
        int index = projectconfig_var_getoffs("projtimemode", &size);
        timeModePtr_ = (int *)projectconfig_var_addr(nullptr, index);
        
        index = projectconfig_var_getoffs("projtimemode2", &size);
        timeMode2Ptr_ = (int *)projectconfig_var_addr(nullptr, index);
        
        index = projectconfig_var_getoffs("projmeasoffs", &size);
        measOffsPtr_ = (int *)projectconfig_var_addr(nullptr, index);
        
        index = projectconfig_var_getoffs("projtimeoffs", &size);
        timeOffsPtr_ = (double *)projectconfig_var_addr(nullptr, index);
        
        index = projectconfig_var_getoffs("panmode", &size);
        projectPanModePtr_ = (int*)projectconfig_var_addr(nullptr, index);
    }
    
    void Shutdown()
    {
        fxParamsDisplay_ = false;
        surfaceInDisplay_ = false;
        surfaceOutDisplay_ = false;
       
        // GAW -- IMPORTANT
        // We want to stop polling and zero out all Widgets before shutting down
        shouldRun_ = false;
        
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->ForceClearAllWidgets();
    }
    
    void Init();

    void ToggleSurfaceInDisplay() { surfaceInDisplay_ = ! surfaceInDisplay_;  }
    void ToggleSurfaceOutDisplay() { surfaceOutDisplay_ = ! surfaceOutDisplay_;  }
    void ToggleFXParamsDisplay() { fxParamsDisplay_ = ! fxParamsDisplay_;  }
    void ToggleFXParamsWrite() { fxParamsWrite_ = ! fxParamsWrite_;  }

    bool GetSurfaceInDisplay() { return surfaceInDisplay_;  }
    bool GetSurfaceOutDisplay() { return surfaceOutDisplay_;  }
    
    double GetFaderMaxDB() { return GetPrivateProfileDouble("slidermaxv"); }
    double GetFaderMinDB() { return GetPrivateProfileDouble("sliderminv"); }
    double GetVUMaxDB() { return GetPrivateProfileDouble("vumaxvol"); }
    double GetVUMinDB() { return GetPrivateProfileDouble("vuminvol"); }
    
    int *GetTimeModePtr() { return timeModePtr_; }
    int *GetTimeMode2Ptr() { return timeMode2Ptr_; }
    int *GetMeasOffsPtr() { return measOffsPtr_; }
    double *GetTimeOffsPtr() { return timeOffsPtr_; }
    int GetProjectPanMode() { return *projectPanModePtr_; }
   
    ActionContext GetActionContext(string actionName, Widget* widget, Zone* zone, vector<string> params, vector<vector<string>> properties)
    {      
        if(actions_.count(actionName) > 0)
            return ActionContext(actions_[actionName], widget, zone, params, properties);
        else
            return ActionContext(actions_["NoAction"], widget, zone, params, properties);
    }

    void OnTrackSelection(MediaTrack *track)
    {
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->OnTrackSelection();
    }
    
    void OnTrackListChange()
    {
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->OnTrackListChange();
    }
    
    void OnFXFocus(MediaTrack *track, int fxIndex)
    {
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->OnFXFocus(track, fxIndex);
    }
    
    void NextTimeDisplayMode()
    {
        int *tmodeptr = GetTimeMode2Ptr();
        if (tmodeptr && *tmodeptr>=0)
        {
            (*tmodeptr)++;
            if ((*tmodeptr)>5)
                (*tmodeptr)=0;
        }
        else
        {
            tmodeptr = GetTimeModePtr();
            
            if (tmodeptr)
            {
                (*tmodeptr)++;
                if ((*tmodeptr)>5)
                    (*tmodeptr)=0;
            }
        }

        if(pages_.size() > 0)
            pages_[currentPageIndex_]->ForceRefreshTimeDisplay();
    }
    
    void AdjustTrackBank(Page* sendingPage, int amount)
    {
        if(! sendingPage->GetTrackNavigationManager()->GetSynchPages())
            sendingPage->GetTrackNavigationManager()->AdjustTrackBank(amount);
        else
            for(auto page: pages_)
                if(page->GetTrackNavigationManager()->GetSynchPages())
                    page->GetTrackNavigationManager()->AdjustTrackBank(amount);
    }

    void AdjustMappedWidgetsBank(Page* sendingPage, int amount)
    {
        if (!sendingPage->GetTrackNavigationManager()->GetSynchPages())
            sendingPage->GetMappedWidgetsNavigationManager()->AdjustMappedWidgetsBank(amount);
        else
            for (auto page : pages_)
                if (page->GetTrackNavigationManager()->GetSynchPages())
                    page->GetMappedWidgetsNavigationManager()->AdjustMappedWidgetsBank(amount);
    }
    
    void NextPage()
    {
        if(pages_.size() > 0)
        {
            pages_[currentPageIndex_]->LeavePage();
            currentPageIndex_ = currentPageIndex_ == pages_.size() - 1 ? 0 : ++currentPageIndex_;
            pages_[currentPageIndex_]->EnterPage();
        }
    }
    
    void GoToPage(string pageName)
    {
        for(int i = 0; i < pages_.size(); i++)
        {
            if(pages_[i]->GetName() == pageName)
            {
                pages_[currentPageIndex_]->LeavePage();
                currentPageIndex_ = i;
                pages_[currentPageIndex_]->EnterPage();
                
                break;
            }
        }
    }
    
    bool GetTouchState(MediaTrack* track, int touchedControl)
    {
        if(pages_.size() > 0)
            return pages_[currentPageIndex_]->GetTouchState(track, touchedControl);
        else
            return false;
    }
    
    void TrackFXListChanged(MediaTrack* track)
    {
        for(auto & page : pages_)
            page->TrackFXListChanged(track);
        
        if(fxParamsDisplay_ || fxParamsWrite_)
        {
            char fxName[BUFSZ];
            char fxParamName[BUFSZ];
            
            ofstream fxFile;
            
            for(int i = 0; i < DAW::TrackFX_GetCount(track); i++)
            {
                DAW::TrackFX_GetFXName(track, i, fxName, sizeof(fxName));
                
                if(fxParamsDisplay_)
                    DAW::ShowConsoleMsg(("Zone \"" + string(fxName) + "\"").c_str());
                
                if(fxParamsWrite_)
                {
                    string fxNameNoBadChars(fxName);
                    fxNameNoBadChars = regex_replace(fxNameNoBadChars, regex(BadFileChars), "_");

                    fxFile.open(string(DAW::GetResourcePath()) + "/CSI/Zones/ZoneRawFXFiles/" + fxNameNoBadChars + ".txt");
                    
                    if(fxFile.is_open())
                        fxFile << "Zone \"" + string(fxName) + "\"" + GetLineEnding();
                }

                if(fxParamsDisplay_)
                    DAW::ShowConsoleMsg("\n\n\tSelectedTrackNavigator\n");
                
                if(fxParamsWrite_ && fxFile.is_open())
                    fxFile << "\tSelectedTrackNavigator" + GetLineEnding();

                for(int j = 0; j < DAW::TrackFX_GetNumParams(track, i); j++)
                {
                    DAW::TrackFX_GetParamName(track, i, j, fxParamName, sizeof(fxParamName));

                    if(fxParamsDisplay_)
                        DAW::ShowConsoleMsg(("\n\tFXParam " + to_string(j) + " \"" + string(fxParamName) + "\"").c_str());
  
                    if(fxParamsWrite_ && fxFile.is_open())
                        fxFile <<  "\tFXParam " + to_string(j) + " \"" + string(fxParamName)+ "\"" + GetLineEnding();
                        
                    /* step sizes
                    double stepOut = 0;
                    double smallstepOut = 0;
                    double largestepOut = 0;
                    bool istoggleOut = false;
                    TrackFX_GetParameterStepSizes(track, i, j, &stepOut, &smallstepOut, &largestepOut, &istoggleOut);

                    DAW::ShowConsoleMsg(("\n\n" + to_string(j) + " - \"" + string(fxParamName) + "\"\t\t\t\t Step = " +  to_string(stepOut) + " Small Step = " + to_string(smallstepOut)  + " LargeStep = " + to_string(largestepOut)  + " Toggle Out = " + (istoggleOut == 0 ? "false" : "true")).c_str());
                    */
                }
                
                if(fxParamsDisplay_)
                    DAW::ShowConsoleMsg("\nZoneEnd\n\n");

                if(fxParamsWrite_ && fxFile.is_open())
                {
                    fxFile << "ZoneEnd";
                    fxFile.close();
                }
            }
        }
    }

    void InitializeEuCon()
    {
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->InitializeEuCon();
    }
    
    //int repeats = 0;
    
    void Run()
    {
        //int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        if(shouldRun_ && pages_.size() > 0)
            pages_[currentPageIndex_]->Run();
        /*
         repeats++;
         
         if(repeats > 50)
         {
         repeats = 0;
         
         int duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
         
         char msgBuffer[250];
         
         sprintf(msgBuffer, "%d microseconds\n", duration);
         DAW::ShowConsoleMsg(msgBuffer);
         }
        */
    }
};

/*
 int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
 
 
 // code you wish to time goes here
 // code you wish to time goes here
 // code you wish to time goes here
 // code you wish to time goes here
 // code you wish to time goes here
 // code you wish to time goes here
 
 
 
 int duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
 
 char msgBuffer[250];
 
 sprintf(msgBuffer, "%d microseconds\n", duration);
 DAW::ShowConsoleMsg(msgBuffer);
 
 */

#endif /* control_surface_integrator.h */
