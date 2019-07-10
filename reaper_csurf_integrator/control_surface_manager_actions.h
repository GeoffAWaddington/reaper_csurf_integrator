//
//  control_surface_manager_actions.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_manager_actions_h
#define control_surface_manager_actions_h

#include "control_surface_integrator.h"

extern Manager* TheManager;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleMapSends  : public ActionOld
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    
    void Do(Page* page, ControlSurface* surface) override
    {
        page->ToggleMapSends(surface);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MapSelectedTrackSendsToWidgets  : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    MapSelectedTrackSendsToWidgets(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, double value) override
    {
        page->MapSelectedTrackSendsToWidgets();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MapSelectedTrackFXToWidgets  : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    MapSelectedTrackFXToWidgets(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, double value) override
    {
        page->MapSelectedTrackFXToWidgets();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MapFocusedTrackFXToWidgets  : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    MapFocusedTrackFXToWidgets(WidgetActionManager* manager) : GlobalAction(manager) {}

    
    void Do(Page* page, double value) override
    {
        page->MapFocusedTrackFXToWidgets();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectTrackRelative : public GlobalActionWithIntParam
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SelectTrackRelative(WidgetActionManager* manager, vector<string> params) : GlobalActionWithIntParam(manager, params) {}

    void Do(Page* page, double stride) override
    {
        if(1 == DAW::CountSelectedTracks(nullptr))
        {
            int trackIndex = 0;
            
            for(int i = 0; i < page->GetNumTracks(); i++)
                if(DAW::GetMediaTrackInfo_Value(page->GetTrackFromId(i), "I_SELECTED"))
                {
                    trackIndex = i;
                    break;
                }
            
            trackIndex += stride;
            
            if(trackIndex < 0)
                trackIndex = 0;
            
            if(trackIndex > page->GetNumTracks() - 1)
                trackIndex = page->GetNumTracks() - 1;
            
            DAW::SetOnlyTrackSelected(page->GetTrackFromId(trackIndex));
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetShowFXWindows : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SetShowFXWindows(WidgetActionManager* manager) : GlobalAction(manager) {}
    
    void RequestUpdate() override
    {
        SetWidgetValue(widget_, page_->GetShowFXWindows());
    }
    
    void Do(Page* page, double value) override
    {
        page->SetShowFXWindows(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetScrollLink : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SetScrollLink(WidgetActionManager* manager) : GlobalAction(manager) {}

    void RequestUpdate() override
    {
        SetWidgetValue(widget_, page_->GetScrollLink());
    }
    
    void Do(Page* page, double value) override
    {
        page->SetScrollLink(! page->GetScrollLink());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTimeDisplayModes : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    CycleTimeDisplayModes(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, double value) override
    {
        int *tmodeptr = &__g_projectconfig_timemode2;
        if (tmodeptr && *tmodeptr>=0)
        {
            (*tmodeptr)++;
            if ((*tmodeptr)>5)
                (*tmodeptr)=0;
        }
        else
        {
            tmodeptr = &__g_projectconfig_timemode;
            
            if (tmodeptr)
            {
                (*tmodeptr)++;
                if ((*tmodeptr)>5)
                    (*tmodeptr)=0;
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoNextPage : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    GoNextPage(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, double value) override
    {
        TheManager->NextPage();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoPage : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    GoPage(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, string value) override
    {
        TheManager->GoPage(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoZone : public ActionOld
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(ControlSurface* surface, string zoneName) override
    {
        surface->GetPage()->GoZone(surface, zoneName);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackBank : public GlobalActionWithIntParam
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    TrackBank(WidgetActionManager* manager, vector<string> params) : GlobalActionWithIntParam(manager, params) {}

    void Do(Page* page, double stride) override
    {
        TheManager->AdjustTrackBank(page, stride);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendBank : public GlobalActionWithIntParam
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    TrackSendBank(WidgetActionManager* manager, vector<string> params) : GlobalActionWithIntParam(manager, params) {}

    void Do(Page* page, double stride) override
    {
        page->AdjustTrackSendBank(stride);
    }
};
/*
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PinSelectedTracks : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double value) override
    {
        page->PinSelectedTracks();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class UnpinSelectedTracks : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double value) override
    {
        page->UnpinSelectedTracks();
    }
};
*/
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetShift : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SetShift(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, double value) override
    {
        page->SetShift(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetOption : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SetOption(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, double value) override
    {
        page->SetOption(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetControl : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SetControl(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, double value) override
    {
        page->SetControl(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetAlt : public GlobalAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SetAlt(WidgetActionManager* manager) : GlobalAction(manager) {}

    void Do(Page* page, double value) override
    {
        page->SetAlt(value);
    }
};

#endif /* control_surface_manager_actions_h */
