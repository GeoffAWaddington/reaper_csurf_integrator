//
//  control_surface_Reaper_actions.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_Reaper_actions_h
#define control_surface_Reaper_actions_h

#include "control_surface_base_actions.h"

extern Manager* TheManager;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ReaperAction : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{    
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, int commandId) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetToggleCommandState(commandId));
    }
    
    void Do(Page* page, double commandId) override
    {
        DAW::SendCommandMessage(commandId);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackFX : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{   
public:
    virtual void RequestUpdate(ActionContext* actionContext, Widget* widget, MediaTrack* track, int fxIndex, int paramIndex)
    {
        double min, max = 0;

        actionContext->SetWidgetValue(widget, DAW::TrackFX_GetParam(track, fxIndex, paramIndex, &min, &max));
    }
    
    virtual void Do(MediaTrack* track, int fxIndex, int paramIndex, double value)
    {
        DAW::TrackFX_SetParam(track, fxIndex, paramIndex, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackVolume : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, volToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_VOL")));
    }
    
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
         DAW::CSurf_SetSurfaceVolume(track, DAW::CSurf_OnVolumeChange(track, normalizedToVol(value), false), NULL);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPan : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_PAN")));
    }
    
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
        DAW::CSurf_SetSurfacePan(track, DAW::CSurf_OnPanChange(track, normalizedToPan(value), false), NULL);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanWidth : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_WIDTH")));
    }
    
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
        DAW::CSurf_OnWidthChange(track, normalizedToPan(value), false);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        string trackName = "";
        
        if(DAW::GetMediaTrackInfo_Value(track , "IP_TRACKNUMBER") == -1)
            trackName = "Master";
        else
            trackName =  (char *)DAW::GetSetMediaTrackInfo(track, "P_NAME", NULL);

        widget->SetValue(trackName);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackVolumeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        char trackVolume[128];
        sprintf(trackVolume, "%7.2lf", VAL2DB(DAW::GetMediaTrackInfo_Value(track, "D_VOL")));
        widget->SetValue(string(trackVolume));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        bool left = false;
        
        double panVal = DAW::GetMediaTrackInfo_Value(track, "D_PAN");
        
        if(panVal < 0)
        {
            left = true;
            panVal = -panVal;
        }
        
        int panIntVal = int(panVal * 100.0);
        string trackPan = "";
        
        if(left)
        {
            if(panIntVal == 100)
                trackPan += "<";
            else if(panIntVal < 100 && panIntVal > 9)
                trackPan += "< ";
            else
                trackPan += "<  ";
            
            trackPan += to_string(panIntVal);
        }
        else
        {
            trackPan += "   ";
            
            trackPan += to_string(panIntVal);
            
            if(panIntVal == 100)
                trackPan += ">";
            else if(panIntVal < 100 && panIntVal > 9)
                trackPan += " >";
            else
                trackPan += "  >";
        }
        
        if(panIntVal == 0)
            trackPan = "  <C>  ";
        
        widget->SetValue(string(trackPan));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanWidthDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        bool reversed = false;
        
        double widthVal = DAW::GetMediaTrackInfo_Value(track, "D_WIDTH");
        
        if(widthVal < 0)
        {
            reversed = true;
            widthVal = -widthVal;
        }
        
        int widthIntVal = int(widthVal * 100.0);
        string trackPanWidth = "";
        
        if(reversed)
            trackPanWidth += "Rev ";
        
        trackPanWidth += to_string(widthIntVal);
        
        if(widthIntVal == 0)
            trackPanWidth = " <Mno> ";

        widget->SetValue(string(trackPanWidth));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Rewind : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double value) override
    {
        DAW::CSurf_OnRew(1);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FastForward : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double value)
    {
        DAW::CSurf_OnFwd(1);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Play : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget) override
    {
        int playState = DAW::GetPlayState();
        if(playState == 1 || playState == 2 || playState == 5 || playState == 6) // playing or paused or recording or paused whilst recording
            playState = 1;
        else playState = 0;
        actionContext->SetWidgetValue(widget, playState);
    }
    
    void Do(Page* page, double value) override
    {
        DAW::CSurf_OnPlay();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Stop : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget) override
    {
        int stopState = DAW::GetPlayState();
        if(stopState == 0 || stopState == 2 || stopState == 6) // stopped or paused or paused whilst recording
            stopState = 1;
        else stopState = 0;
        
        actionContext->SetWidgetValue(widget, stopState);
    }
    
    void Do(Page* page, double value) override
    {
        DAW::CSurf_OnStop();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Record : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget) override
    {
        int recordState = DAW::GetPlayState();
        if(recordState == 5 || recordState == 6) // recording or paused whilst recording
            recordState = 1;
        else recordState = 0;
        
        actionContext->SetWidgetValue(widget, recordState);
    }
    
    void Do(Page* page, double value) override
    {
        DAW::CSurf_OnRecord();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class RepeatingArrow : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    /*
private:
    int direction_ = 0;
    clock_t lastRepeated = clock();
    double repeatRate_ = 0.0;
    bool pressed_ = false;
    
public:
    
    virtual void Update(string zoneName, string surfaceName, string widgetName) override
    {
        if(pressed_ && clock() - lastRepeated >  CLOCKS_PER_SEC * repeatRate_)
        {
            lastRepeated = clock();
            // GAW TBD
            //DAW::CSurf_OnArrow(direction_, Getlayer->GetRealSurfaceFor(surfaceName)->IsZoom());
        }
    }
    
    virtual void Do(double value, string zoneName, string surfaceName, string widgetName) override
    {
        // GAW TBD
        // DAW::CSurf_OnArrow(direction_, Getlayer->GetRealSurfaceFor(surfaceName)->IsZoom());
        pressed_ = value;
    }
     */
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSelect : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetMediaTrackInfo_Value(track, "I_SELECTED"));
    }
    
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
        DAW::CSurf_SetSurfaceSelected(track, DAW::CSurf_OnSelectedChange(track, ! DAW::GetMediaTrackInfo_Value(track, "I_SELECTED")), NULL);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackUniqueSelect : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetMediaTrackInfo_Value(track, "I_SELECTED"));
    }
    
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
        DAW::SetOnlyTrackSelected(track);
        TheManager->OnTrackSelection(track);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackRangeSelect : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetMediaTrackInfo_Value(track, "I_SELECTED"));
    }

    virtual void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
        int currentlySelectedCount = 0;
        int selectedTrackIndex = 0;
        int trackIndex = 0;
        
        for(int i = 0; i < DAW::CSurf_NumTracks(page->GetFollowMCP()); i++)
        {
           MediaTrack* currentTrack = DAW::CSurf_TrackFromID(i, page->GetFollowMCP());
            
            if(currentTrack == track)
                trackIndex = i;
            
            if(DAW::GetMediaTrackInfo_Value(currentTrack, "I_SELECTED"))
            {
                selectedTrackIndex = i;
                currentlySelectedCount++;
            }
        }
        
        if(currentlySelectedCount != 1)
            return;
        
        int lowerBound = trackIndex < selectedTrackIndex ? trackIndex : selectedTrackIndex;
        int upperBound = trackIndex > selectedTrackIndex ? trackIndex : selectedTrackIndex;

        for(int i = lowerBound; i <= upperBound; i++)
        {
            MediaTrack* currentTrack = DAW::CSurf_TrackFromID(i, page->GetFollowMCP());
            
            DAW::CSurf_SetSurfaceSelected(currentTrack, DAW::CSurf_OnSelectedChange(currentTrack, 1), NULL);
            TheManager->OnTrackSelection(currentTrack);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackRecordArm : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetMediaTrackInfo_Value(track, "I_RECARM"));
    }
    
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
        DAW::CSurf_SetSurfaceRecArm(track, DAW::CSurf_OnRecArmChange(track, ! DAW::GetMediaTrackInfo_Value(track, "I_RECARM")), NULL);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackMute : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetMediaTrackInfo_Value(track, "B_MUTE"));
    }
    
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
        DAW::CSurf_SetSurfaceMute(track, DAW::CSurf_OnMuteChange(track, ! DAW::GetMediaTrackInfo_Value(track, "B_MUTE")), NULL);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSolo : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetMediaTrackInfo_Value(track, "I_SOLO"));
    }
    
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
        DAW::CSurf_SetSurfaceSolo(track, DAW::CSurf_OnSoloChange(track, ! DAW::GetMediaTrackInfo_Value(track, "I_SOLO")), NULL);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackTouch : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, Widget* widget, MediaTrack* track, double value) override
    {
       page->SetTouchState(track, value == 0 ? false : true);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackTouchControlled : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track, int param) override
    {
        if(MediaTrack* track = DAW::GetTrackFromGUID(widget->GetTrackGUID(), page->GetFollowMCP()))
            if(page->GetTouchState(track, 0))
                actionContext->RequestActionUpdate(page, widget);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GlobalAutoMode : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetGlobalAutomationOverride());
    }
    
    void Do(Page* page, double autoMode) override
    {
         DAW::SetGlobalAutomationOverride(autoMode);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackAutoMode : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        for(int i = 0; i < DAW::CSurf_NumTracks(page->GetFollowMCP()); i++)
            if(MediaTrack* currentTrack = DAW::CSurf_TrackFromID(i, page->GetFollowMCP()))
                if(DAW::GetMediaTrackInfo_Value(currentTrack, "I_SELECTED"))
                    actionContext->SetWidgetValue(widget, DAW::GetMediaTrackInfo_Value(currentTrack, "I_AUTOMODE"));
    }
    
    virtual void Do(Page* page, double autoMode) override
    {
         DAW::SetAutomationMode(autoMode, true);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTimeline : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget) override
    {
        actionContext->SetWidgetValue(widget, DAW::GetSetRepeatEx(nullptr, -1));
    }
    
    void Do(Page* page, double value) override
    {
        DAW::GetSetRepeatEx(nullptr, ! DAW::GetSetRepeatEx(nullptr, -1));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackOutputMeter : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track, int param) override
    {
        if(DAW::GetPlayState() & 0x01) // if playing
            actionContext->SetWidgetValue(widget, VAL2DB(DAW::Track_GetPeakInfo(track, param)));
        else
            actionContext->SetWidgetValue(widget, 0.0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackGainReductionMeter : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget, MediaTrack* track) override
    {
        if(DAW::GetPlayState() & 0x01) // if playing
        {
            char buffer[BUFSZ];

            // GAW TBD fx Index
            int fxIndex = 0;
            
            if(DAW::TrackFX_GetNamedConfigParm(track, fxIndex, "GainReduction_dB", buffer, sizeof(buffer)))
               actionContext->SetWidgetValue(widget, -atof(buffer)/20.0);
            else
               actionContext->SetWidgetValue(widget, 0.0);
        }
        else
            actionContext->SetWidgetValue(widget, 1.0);
    }
};

#endif /* control_surface_Reaper_actions_h */
