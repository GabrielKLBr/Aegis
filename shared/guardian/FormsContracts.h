#pragma once
namespace guardian::cli {
enum class FormsKind:uint32_t {None,Application,Control,Form,Button,Label,TextBox,Collection,Component,Delegate,EventArgs};
inline FormsKind FormsType(const char* assembly,const char* name) noexcept {
    if(Equal(assembly,"mscorlib")){
        if(Equal(name,"System.EventHandler"))return FormsKind::Delegate;
        if(Equal(name,"System.EventArgs"))return FormsKind::EventArgs;
    }
    if(Equal(assembly,"System")&&Equal(name,"System.ComponentModel.Component"))return FormsKind::Component;
    if(!Equal(assembly,"System.Windows.Forms"))return FormsKind::None;
    if(Equal(name,"System.Windows.Forms.Application"))return FormsKind::Application;
    if(Equal(name,"System.Windows.Forms.Control"))return FormsKind::Control;
    if(Equal(name,"System.Windows.Forms.Form"))return FormsKind::Form;
    if(Equal(name,"System.Windows.Forms.Button"))return FormsKind::Button;
    if(Equal(name,"System.Windows.Forms.Label"))return FormsKind::Label;
    if(Equal(name,"System.Windows.Forms.TextBox"))return FormsKind::TextBox;
    // Nested TypeRef names are normalized by Image::TypeName.
    if(Equal(name,"System.Windows.Forms.Control+ControlCollection"))return FormsKind::Collection;
    return FormsKind::None;
}
inline bool FormsControl(FormsKind kind) noexcept {
    return kind==FormsKind::Form||kind==FormsKind::Button||kind==FormsKind::Label||kind==FormsKind::TextBox;
}
inline bool FormsCall(FormsKind kind,const char* name,const Signature& s) noexcept {
    const bool v=s.result==Type::Void,b=s.result==Type::Bool,i=s.result==Type::I4,o=s.result==Type::Object;
    if(Equal(name,".ctor")&&s.instance&&v){
        if(FormsControl(kind)||kind==FormsKind::EventArgs)return s.count==0;
        return kind==FormsKind::Delegate&&s.count==2&&s.args[0]==Type::Object&&s.args[1]==Type::NativeInt;
    }
    if(kind==FormsKind::Application&&!s.instance){
        if(Equal(name,"Run"))return v&&(s.count==0||(s.count==1&&s.args[0]==Type::Object));
        if(Equal(name,"Exit")||Equal(name,"DoEvents"))return v&&!s.count;
        // Visual styles/text rendering require their actual implementation.
        return false;
    }
    if(!s.instance)return false;
    if(kind==FormsKind::Collection)return Equal(name,"Add")&&v&&s.count==1&&s.args[0]==Type::Object;
    if(kind==FormsKind::Component||kind==FormsKind::Control||FormsControl(kind)){
        if(Equal(name,"add_Disposed")||Equal(name,"remove_Disposed")||Equal(name,"add_Click")||Equal(name,"remove_Click"))return v&&s.count==1&&s.args[0]==Type::Object;
        if(Equal(name,"Dispose")||Equal(name,"Show")||Equal(name,"Hide")||Equal(name,"Close")||Equal(name,"PerformClick"))return v&&!s.count;
        if(Equal(name,"get_Controls"))return o&&!s.count;
        if(Equal(name,"get_Text"))return s.result==Type::String&&!s.count;
        if(Equal(name,"set_Text"))return v&&s.count==1&&s.args[0]==Type::String;
        if(Equal(name,"get_Visible")||Equal(name,"get_IsDisposed"))return b&&!s.count;
        if(Equal(name,"set_Visible"))return v&&s.count==1&&s.args[0]==Type::Bool;
        const char* properties[]={"Left","Top","Width","Height"};
        for(const auto* property:properties){
            char get[24]="get_",set[24]="set_";uint32_t n=0;while(property[n]){get[4+n]=set[4+n]=property[n];++n;}
            if(Equal(name,get))return i&&!s.count;
            if(Equal(name,set))return v&&s.count==1&&s.args[0]==Type::I4;
        }
    }
    return false;
}
}
