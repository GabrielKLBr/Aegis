// WscIntegration.h - Windows Security Center (WSC) integration.
//
// IMPORTANT REALITY CHECK
// -----------------------
// The Windows Security app reads registered antivirus products from the WMI
// namespace  ROOT\SecurityCenter2  (class AntiVirusProduct).  Those records are
// populated by Windows itself when a product registers through the protected
// WSC API.  A third-party product is only allowed to register (and to make
// Microsoft Defender stand down) if it satisfies ALL of the following, which
// Microsoft gates behind the Microsoft Virus Initiative (MVI):
//
//   1. Membership in the Microsoft Virus Initiative (MVI).
//   2. An Early-Launch Anti-Malware (ELAM) driver, so the registering service
//      can run as a Protected Process Light (PPL / anti-malware light).
//   3. Binaries code-signed with the certificate Microsoft issues to MVI
//      members.
//
// Without those, WscRegisterSecurityProduct() returns an access/privilege error
// and the product will NOT appear in the Security panel.  This is an
// intentional security boundary - the same one malware tries (and fails) to
// cross.  The functions below therefore let you INSPECT what the panel shows
// (fully working) and attempt the documented registration call (which will
// fail without MVI, by design - we report exactly why).
#pragma once
#include "Common.h"

namespace av {

// Queries ROOT\SecurityCenter2 and prints every registered antivirus product
// plus the decoded product state, exactly as the Security panel reads it.
// Returns false if WMI could not be reached.
bool ListSecurityProducts();

// Attempts the documented WSC registration for this product.
// On a normal (non-MVI, non-PPL) build this is expected to fail; the function
// explains the result. 'displayName' is the name that *would* be shown.
void AttemptRegister(const std::wstring& displayName);

} // namespace av
