// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Brand-specific types and constants for Chromium.

#ifndef CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_
#define CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_

#include <array>

#include "chrome/app/chrome_dll_resource.h"
#include "chrome/common/chrome_icon_resources_win.h"
#include "chrome/install_static/install_constants.h"

namespace install_static {

// The brand-specific company name to be included as a component of the install
// and user data directory paths. May be empty if no such dir is to be used.
inline constexpr wchar_t kCompanyPathName[] = L"";

// The brand-specific product name to be included as a component of the install
// and user data directory paths. Pubky Chromium uses its own name so that it
// does not share files, registry keys or profiles with upstream Chromium.
inline constexpr wchar_t kProductPathName[] = L"PubkyChromium";

// The brand-specific safe browsing client name.
inline constexpr char kSafeBrowsingName[] = "chromium";

// Note: This list of indices must be kept in sync with the brand-specific
// resource strings in chrome/installer/util/prebuild/create_string_rc.
enum InstallConstantIndex {
  CHROMIUM_INDEX,
  NUM_INSTALL_MODES,
};

inline constexpr auto kOldTracingServiceIids = std::to_array<IID>({
    // Replaced in 2026-09. Delete after 2028-09.
    // {A3FD580A-FFD4-4075-9174-75D0B199D3CB}
    {0xa3fd580a,
     0xffd4,
     0x4075,
     {0x91, 0x74, 0x75, 0xd0, 0xb1, 0x99, 0xd3, 0xcb}},
});

inline constexpr auto kInstallModes = std::to_array<InstallConstants>({
    // The primary (and only) install mode for Chromium.
    {
        .size = sizeof(InstallConstants),
        .index = CHROMIUM_INDEX,  // The one and only mode for Chromium.
        .install_switch =
            "",  // No install switch for the primary install mode.
        .install_suffix =
            L"",  // Empty install_suffix for the primary install mode.
        .logo_suffix = L"",  // No logo suffix for the primary install mode.
        .app_guid =
            L"",  // Empty app_guid since no integration with Google Update.
        // Pubky Chromium registers its own names, ProgIDs, Active Setup GUID
        // and COM CLSIDs so that it can be installed beside upstream Chromium.
        // The IIDs below are compiled into the shared IDL and stay unchanged.
        .base_app_name = L"Pubky Chromium",     // A distinct base_app_name.
        .base_app_id = L"PubkyChromium",        // A distinct base_app_id.
        .browser_prog_id_prefix = L"PubkyHTM",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Pubky Chromium HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "pubky-chromium",
        .pdf_prog_id_prefix = L"PubkyPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Pubky Chromium PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{8E92E86A-AE8B-4E7F-9149-86E1FA8DF8B8}",  // Active Setup
                                                        // GUID.
        .toast_activator_clsid = {0xEFFC8FED,
                                  0xB819,
                                  0x43DE,
                                  {0xA6, 0xC4, 0x07, 0xB7, 0x02, 0x0B, 0xE8,
                                   0x41}},  // Toast Activator CLSID.
        // {EFFC8FED-B819-43DE-A6C4-07B7020BE841}
        .elevator_clsid = {0x93F7D151,
                           0xA76F,
                           0x4DEC,
                           {0xBD, 0xB2, 0x0C, 0xFB, 0x12, 0x2D, 0x43,
                            0xAC}},  // Elevator CLSID.
        // {93F7D151-A76F-4DEC-BDB2-0CFB122D43AC}
        .elevator_iid = {0xbb19a0e5,
                         0xc6,
                         0x4966,
                         {0x94, 0xb2, 0x5a, 0xfe, 0xc6, 0xfe, 0xd9,
                          0x3a}},  // IElevator IID and TypeLib
        // {BB19A0E5-00C6-4966-94B2-5AFEC6FED93A}.
        .old_elevator_iids = {},
        .tracing_service_clsid = {0x7FA826DA,
                                  0xAF56,
                                  0x4192,
                                  {0x8E, 0x5C, 0x4A, 0x26, 0xF6, 0xEB, 0xF4,
                                   0x42}},  // SystemTraceSession CLSID.
        // {7FA826DA-AF56-4192-8E5C-4A26F6EBF442}
        .tracing_service_iid = {0xe0b03e2d,
                                0x7682,
                                0x4d83,
                                {0xb9, 0xff, 0x45, 0x74, 0xaf, 0x72, 0x05,
                                 0x00}},  // ISystemTraceSessionChromium IID and
                                          // TypeLib
        .old_tracing_service_iids = kOldTracingServiceIids,
        .default_channel_name =
            L"",  // Empty default channel name since no update integration.
        .channel_strategy = ChannelStrategy::UNSUPPORTED,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_MAINFRAME,  // App icon resource id.
        .html_doc_icon_resource_index =
            icon_resources::kHtmlDocIndex,  // HTML doc icon resource index.
        .pdf_doc_icon_resource_index =
            icon_resources::kPDFDocIndex,  // PDF doc icon resource index.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-"
            L"1938553385-"
            L"924012148-",  // App container sid prefix for sandbox.
    },
});

}  // namespace install_static

#endif  // CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_
