// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Actual production page, shared application service and real RmlUi layout.
#define main SystemPresetFixtureMain
#include "UiSystemPresetRuntimeTest.cpp"
#undef main

static void Contained(const Bounds& inner,const Bounds& outer,const char* message) {
    if(inner.x<outer.x-1 || inner.y<outer.y-1 || inner.x+inner.width>outer.x+outer.width+1 || inner.y+inner.height>outer.y+outer.height+1)
        std::fprintf(stderr,"%s: inner=%g,%g %gx%g outer=%g,%g %gx%g\n",message,inner.x,inner.y,inner.width,inner.height,outer.x,outer.y,outer.width,outer.height);
    Check(inner.x>=outer.x-1 && inner.y>=outer.y-1 && inner.x+inner.width<=outer.x+outer.width+1 && inner.y+inner.height<=outer.y+outer.height+1,message);
}
static void PairedField(View& v,const char* slider) {
    const auto number=std::string(slider)+"_number";
    Check(v.runtime.FocusControl(slider,v.time),"paired slider can receive focus");v.Frame();
    const auto control=v.Box(slider),field=v.Box((number+"-viewport").c_str());
    Contained(control,v.Box("settings-body"),"focused slider stays wholly visible");
    Contained(field,v.Box("settings-body"),"focusing a slider reveals its complete paired numeric readout");
    Check(std::abs(control.y-field.y)<1 && std::abs(control.height-field.height)<1,
          "slider and numeric viewport share aligned text-relative heights");
    const auto track=v.Box((std::string(slider)+"-track").c_str());
    Check(std::abs(track.y+track.height*.5f-control.y-control.height*.5f)<1,
          "slider track remains centered as the numeric field grows");
    const auto before=v.Element("settings-body")->GetScrollTop();v.Frame();v.Frame();
    Check(v.Element("settings-body")->GetScrollTop()==before,"paired field reveal does not creep on unchanged frames");
    Check(v.runtime.FocusControl(number,v.time),"paired number can receive focus");v.Frame();
    Contained(v.Box(slider),v.Box("settings-body"),"focusing the number also leaves its slider visible");
    Contained(v.Box((number+"-viewport").c_str()),v.Box("settings-body"),"focused numeric viewport stays complete");
    Check(v.runtime.TakeActions().empty(),"revealing paired controls does not edit the settings draft");
}
static void ModalBody(View& v,const char* name,const char* title) {
    const auto id=std::string(name)+"_body_scrollbar";
    const auto body=v.Box((std::string(name)+"-body").c_str());
    if(body.height<v.Element(title)->GetComputedValues().line_height().value-1)
        std::fprintf(stderr,"modal %s body height=%g line=%g\n",name,body.height,v.Element(title)->GetComputedValues().line_height().value);
    Check(body.height>=v.Element(title)->GetComputedValues().line_height().value-1,"modal retains at least one complete heading line");
    TextFits(v,title);
    auto state=v.runtime.GetWidgetState(id);
    Check(state&&state->scroll&&state->scroll->available,"modal body scrollbar has measured geometry");
    if(state->scroll->geometry.usable) {
        Check(v.runtime.FocusControl(id,v.time),"overflowing modal body has reachable authored scrollbar");
        v.Key(MenuInput::End);v.Frame();
        Check(v.Element((std::string(name)+"-body").c_str())->GetScrollTop()>0,"modal instructions can scroll to their end");
        Check(v.runtime.TakeActions().empty(),"scrolling modal instructions does not choose a decision");
    }
}
static void CheckPage(View& v,int width,int height,float density,float textScale) {
    std::fprintf(stderr,"SYSTEM text viewport=%dx%d density=%g text=%g\n",width,height,density,textScale);
    v.viewport.width=width;v.viewport.height=height;v.viewport.displayScale=density;v.viewport.textScale=textScale;
    v.Sync();v.Frame();
    const Bounds screen{0,0,float(width),float(height)};
    v.message="#str_230007";v.Sync();v.Frame();
    Check(v.runtime.FocusControl("settings_brightness",v.time),"focus slider before a same-frame status change");
    v.message="#str_230014";v.Sync();v.Frame();
    Contained(v.Box("settings_brightness"),v.Box("settings-body"),"same-frame focus and dirty status keep the slider wholly visible");
    Contained(v.Box("settings_brightness_number-viewport"),v.Box("settings-body"),"same-frame focus and dirty status keep the paired value wholly visible");
    for(auto* status:{"#str_230007","#str_230008","#str_230009","#str_230010","#str_230011","#str_230012","#str_230013","#str_230014"}) {
        Check(v.runtime.FocusControl("settings_brightness",v.time),"focus slider before changing status");v.Frame();
        v.message=status;v.Sync();v.Frame();
        Contained(v.Box("settings_brightness"),v.Box("settings-body"),"status reflow keeps the existing slider focus wholly visible");
        Contained(v.Box("settings_brightness_number-viewport"),v.Box("settings-body"),"status reflow keeps the paired numeric value wholly visible");
        TextFits(v,"settings-message");
        Contained(v.Box("settings-footer"),screen,"long status cannot displace the actions");
        Check(v.Box("settings-body").height>=3*16*density*textScale-1,"long status cannot consume the reserved control viewport");
        Check(v.Box("settings-header-body").height>=34*density*textScale-1,"header retains a complete enlarged title line");
        auto state=v.runtime.GetWidgetState("settings_header_scrollbar");
        Check(state&&state->scroll&&state->scroll->available,"status scrollbar has measured geometry");
        if(state->scroll->geometry.usable) {
            Check(v.runtime.FocusControl("settings_header_scrollbar",v.time),"long status is reachable through the authored scrollbar");
            v.Key(MenuInput::End);v.Frame();
            Check(v.Element("settings-header-body")->GetScrollTop()>0,"long status can scroll to its end");
            const auto message=v.Box("settings-message"),header=v.Box("settings-header-body");
            Check(message.y+message.height<=header.y+header.height+1 && message.y+message.height>=header.y,
                  "last status line is actually inside the header after End");
            Check(v.runtime.TakeActions().empty(),"reading status does not apply or discard settings");
            v.Key(MenuInput::Home);v.Frame();
        }
    }
    Contained(v.Box("settings-footer"),screen,"essential footer stays inside the viewport");
    Contained(v.Box("settings_back"),screen,"Back remains reachable");
    Contained(v.Box("settings_apply"),screen,"Apply remains reachable");
    for(auto* id:{"settings-title","settings-message","settings_back-label","settings_apply-label"})TextFits(v,id);
    if(v.Box("settings-body").height<=36*density)
        for(auto* id:{"settings-title","settings-message","settings-footer","settings-actions","settings-body"}) {
            auto b=v.Box(id);std::fprintf(stderr,"page %s = %g,%g %gx%g\n",id,b.x,b.y,b.width,b.height);
        }
    Check(v.Box("settings-body").height>36*density,"scroll body retains usable space");
    Check(std::abs(v.Element("settings-title")->GetComputedValues().font_size()-28*density*textScale)<.05,"actual title respects independent text scaling");
    Check(std::abs(v.Element("screen")->GetBox().GetEdge(Rml::BoxArea::Padding,Rml::BoxEdge::Left)-16*density)<.05,"outer furniture padding remains dp");
    for(const auto* slider:{"settings_brightness","settings_ambient"})PairedField(v,slider);
    Check(v.runtime.FocusControl("settings_brightness_number",v.time),"large-text numeric field can receive focus");v.Frame();
    auto viewport=v.Box("settings_brightness_number-viewport");
    auto* text=v.Element("settings_brightness_number-text");
    const auto font=text->GetComputedValues().font_size();
    if(viewport.height>v.Box("settings-body").height)
        for(auto* id:{"settings-title","settings-message","settings-footer","settings-actions","settings_back","settings_apply","settings-body"}) {
            auto b=v.Box(id);std::fprintf(stderr,"field space %s = %g,%g %gx%g\n",id,b.x,b.y,b.width,b.height);
        }
    Check(std::abs(font-16*density*textScale)<.05,"number uses same scaled font as drawing");
    Contained(viewport,v.Box("settings-body"),"focus reveals the complete numeric text viewport");
    Check(v.runtime.BeginNumberEdit("settings_brightness_number",v.error,v.time),"numeric editor begins at enlarged size");v.Frame();
    auto field=v.runtime.GetWidgetState("settings_brightness_number")->number;
    Check(field.has_value(),"editor retains typed identity");
    Contained(v.Box("settings_brightness_number-caret"),viewport,"scaled caret fits the numeric viewport");
    v.Key(MenuInput::Back);v.Frame();
    for(auto* id:{"settings_preset","settings_fullscreen_policy","settings_msaa","settings_postaa","settings_resolution_scale","settings_vsync"}) {
        Check(v.runtime.FocusControl(id,v.time),"large-text choice can receive focus");v.Frame();
        v.Key(MenuInput::Accept);v.Frame();
        auto state=v.runtime.GetWidgetState(id);
        if(!state || !state->popupOpen) {
            const auto body=v.Box("settings-body"),anchor=v.Box(id);
            std::fprintf(stderr,"closed %s: body=%g,%g %gx%g anchor=%g,%g %gx%g\n",id,body.x,body.y,body.width,body.height,anchor.x,anchor.y,anchor.width,anchor.height);
        }
        Check(state&&state->popupOpen,"large-text popup remains usable");
        auto popup=v.Box((std::string(id)+"-popup").c_str());
        Contained(popup,v.Box("settings-body"),"popup respects the actual scrolling body's safe area");
        v.Key(MenuInput::End);v.Frame();
        auto current=v.runtime.GetWidgetState(id);
        Check(current&&current->popupOpen&&current->scroll&&current->scroll->available,"popup scrolling remains measured at enlarged text");
        TextFits(v,(current->highlight+"-label").c_str());
        Contained(v.Box(current->highlight.c_str()),v.Box((std::string(id)+"-viewport").c_str()),"highlighted enlarged option remains entirely visible");
        v.Key(MenuInput::Back);v.Frame();Check(v.runtime.TakeActions().empty(),"navigation and text reflow do not edit accepted values");
    }
    Check(v.tx.Edit(View::Owner,{{"r_brightness",1.2}}).code==SettingsCode::Ok,"seed dirty state for actual discard modal");v.Sync();
    v.Event("onBack");v.Frame();
    if(v.Box("discard_changes").y+v.Box("discard_changes").height>height+1) {
        for(auto* id:{"discard-panel-plate","discard-scroll-region","discard-body","discard-panel-title","discard-actions"}) {
            auto b=v.Box(id);std::fprintf(stderr,"modal %s = %g,%g %gx%g\n",id,b.x,b.y,b.width,b.height);
        }
    }
    Contained(v.Box("discard-panel-plate"),screen,"enlarged discard modal remains visible");
    for(auto* id:{"discard_apply_changes","discard_keep_editing","discard_changes"}) {
        Contained(v.Box(id),screen,"all discard choices remain reachable");
        TextFits(v,(std::string(id)+"-label").c_str());
    }
    ModalBody(v,"discard","discard-panel-title");
    v.Event("continueEditing");v.Frame();
    Check(v.tx.Edit(View::Owner,{{"r_brightness",1.0}}).code==SettingsCode::Ok,"restore untouched setting after modal check");v.Sync();v.Frame();
    Check(v.runtime.SetState({{"settings.confirmationVisible",true},{"settings.canConfirm",true},{"settings.canRevert",true}},v.error,v.time),"show authored confirmation for layout qualification");v.Frame();
    Contained(v.Box("confirmation-panel-plate"),screen,"enlarged confirmation remains visible");
    for(auto* id:{"settings_keep","settings_revert"}) {
        Contained(v.Box(id),screen,"both confirmation choices stay reachable");
        TextFits(v,(std::string(id)+"-label").c_str());
    }
    ModalBody(v,"confirmation","confirmation-panel-title");
    v.Sync();v.Frame();
}
int main(int argc,char** argv) {
    Check(argc==3 || argc==4,"page, locale directory and optional single-locale arguments");
    const auto source=Read(argv[1]);const std::string directory=argv[2];
    for(auto* locale:{"english","spanish","polish","russian","french","italian"}) {
        if(argc==4 && std::string(argv[3])!=locale)continue;
        for(float expansion:{1.f,1.4f}) {
        std::fprintf(stderr,"Text scale locale=%s glyph expansion=%g\n",locale,expansion);
        View view(source,directory+"/"+locale+"_openq4.lang",1,1280,720,expansion,directory+"/"+locale+"_guis.lang");
        for(float scale:{1.f,1.5f,2.f})CheckPage(view,640,480,1,scale);
        CheckPage(view,1280,720,1.25f,2);
        CheckPage(view,1280,720,2,2);
        CheckPage(view,1920,1080,1.5f,1.5f);
        CheckPage(view,3440,1440,2,2);
        }
    }
    std::printf("SYSTEM text scale: %u checks passed\n",checks);
}
