#pragma once
#include <Windows.h>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace aegis::security {
enum class ProcessRole { Supervisor, Leaf };
inline constexpr DWORD64 ImageMitigations =
    PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON |
    PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON |
    PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON |
    PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON;
inline constexpr DWORD UiRestrictions = JOB_OBJECT_UILIMIT_HANDLES |
    JOB_OBJECT_UILIMIT_READCLIPBOARD | JOB_OBJECT_UILIMIT_WRITECLIPBOARD |
    JOB_OBJECT_UILIMIT_GLOBALATOMS | JOB_OBJECT_UILIMIT_DESKTOP |
    JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | JOB_OBJECT_UILIMIT_EXITWINDOWS |
    JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS;

// Storage and policy values must outlive CreateProcess*. Unsupported security
// attributes fail the launch; never retry with a weaker policy.
class ProcessAttributes {
    std::vector<unsigned char> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
    DWORD64 mitigations_ = ImageMitigations;
    static void Require(BOOL ok) {
        if (!ok) throw std::runtime_error("Cannot apply process launch policy (" +
                                         std::to_string(GetLastError()) + ")");
    }
public:
    explicit ProcessAttributes(std::span<const HANDLE> handles = {}) {
        const DWORD count = 1 + (!handles.empty() ? 1 : 0);
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, count, 0, &bytes);
        storage_.resize(bytes);
        auto* candidate = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        Require(InitializeProcThreadAttributeList(candidate, count, 0, &bytes));
        list_ = candidate;
        try {
            Require(UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                                              &mitigations_, sizeof(mitigations_), nullptr, nullptr));
            if (!handles.empty())
                Require(UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    const_cast<HANDLE*>(handles.data()), handles.size_bytes(), nullptr, nullptr));
        } catch (...) { DeleteProcThreadAttributeList(list_); list_ = nullptr; throw; }
    }
    ~ProcessAttributes() { if (list_) DeleteProcThreadAttributeList(list_); }
    ProcessAttributes(const ProcessAttributes&) = delete;
    ProcessAttributes& operator=(const ProcessAttributes&) = delete;
    LPPROC_THREAD_ATTRIBUTE_LIST Get() const { return list_; }
};
inline bool HasProcessPolicy(HANDLE process, ProcessRole role) {
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY image{};
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extension{};
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY children{};
    return GetProcessMitigationPolicy(process, ProcessImageLoadPolicy, &image, sizeof(image)) &&
        image.NoRemoteImages && image.NoLowMandatoryLabelImages && image.PreferSystem32Images &&
        GetProcessMitigationPolicy(process, ProcessExtensionPointDisablePolicy, &extension, sizeof(extension)) &&
        extension.DisableExtensionPoints && (role == ProcessRole::Supervisor ||
        (GetProcessMitigationPolicy(process, ProcessChildProcessPolicy, &children, sizeof(children)) &&
         children.NoChildProcessCreation));
}
inline bool SealLeafPolicy() {
    // Applying the token child-creation attribute before USER32 initialization
    // fails DLL startup on the tested Windows build for the parser/GUI helpers.
    // Seal immediately after trusted DLL initialization, before accepting any
    // guest commands; the supervisor checks the resulting policy handshake.
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY children{};
    children.NoChildProcessCreation = 1;
    return SetProcessMitigationPolicy(ProcessChildProcessPolicy, &children, sizeof(children)) != FALSE;
}
} // namespace aegis::security
