// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/child_process_sandbox_support_impl_linux.h"

#include <stddef.h>
#include <sys/stat.h>

#include <limits>
#include <memory>

#include "base/pickle.h"
#include "base/posix/eintr_wrapper.h"
#include "base/posix/unix_domain_socket.h"
#include "base/sys_byteorder.h"
#include "base/trace_event/trace_event.h"
#include "components/services/font/public/cpp/font_loader.h"
#include "components/services/font/public/interfaces/constants.mojom.h"
#include "components/services/font/public/interfaces/font_service.mojom.h"
#include "content/public/common/common_sandbox_support_linux.h"
#include "services/service_manager/sandbox/linux/sandbox_linux.h"
#include "services/service_manager/zygote/common/common_sandbox_support_linux.h"
#include "third_party/blink/public/platform/linux/web_fallback_font.h"
#include "third_party/blink/public/platform/web_font_render_style.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_vector.h"

#if defined(CASTANETS)
#include "ui/gfx/font_fallback_linux.h"
#include "ui/gfx/font_render_params.h"
#endif

namespace content {

void GetFallbackFontForCharacter(sk_sp<font_service::FontLoader> font_loader,
                                 int32_t character,
                                 const char* preferred_locale,
                                 blink::WebFallbackFont* fallback_font) {
  DCHECK(font_loader.get());
#if defined(CASTANETS)
  auto fallback_font_ = gfx::GetFallbackFontForChar(character, preferred_locale);

  font_service::mojom::FontIdentityPtr identity(font_service::mojom::FontIdentity::New());
  identity->id = 0;
  identity->ttc_index = fallback_font_.ttc_index;
  identity->str_representation = fallback_font_.filename;

  std::string family_name = fallback_font_.name;
  fallback_font->name =
      blink::WebString::FromUTF8(family_name.c_str(), family_name.length());
  fallback_font->fontconfig_interface_id = 0;
  fallback_font->filename.Assign(identity->str_representation.c_str(),
                                 identity->str_representation.length());
  fallback_font->ttc_index = fallback_font_.ttc_index;
  fallback_font->is_bold = fallback_font_.is_bold;
  fallback_font->is_italic = fallback_font_.is_italic;
#else
  font_service::mojom::FontIdentityPtr font_identity;

  bool is_bold = false;
  bool is_italic = false;
  std::string family_name;
  if (!font_loader->FallbackFontForCharacter(character, preferred_locale,
                                             &font_identity, &family_name,
                                             &is_bold, &is_italic)) {
    LOG(ERROR) << "FontService fallback request does not receive a response.";
    return;
  }

  // TODO(drott): Perhaps take WebFallbackFont out of the picture here and pass
  // mojo FontIdentityPtr directly?
  fallback_font->name =
      blink::WebString::FromUTF8(family_name.c_str(), family_name.length());
  fallback_font->fontconfig_interface_id = font_identity->id;
  fallback_font->filename.Assign(font_identity->str_representation.c_str(),
                                 font_identity->str_representation.length());
  fallback_font->ttc_index = font_identity->ttc_index;
  fallback_font->is_bold = is_bold;
  fallback_font->is_italic = is_italic;
#endif
  return;
}

void GetRenderStyleForStrike(sk_sp<font_service::FontLoader> font_loader,
                             const char* family,
                             int size,
                             bool is_bold,
                             bool is_italic,
                             float device_scale_factor,
                             blink::WebFontRenderStyle* out) {
  DCHECK(font_loader.get());
  font_service::mojom::FontIdentityPtr font_identity;

  *out = blink::WebFontRenderStyle();

  if (size < 0 || size > std::numeric_limits<uint16_t>::max())
    return;

  font_service::mojom::FontRenderStylePtr font_render_style;
  if (!font_loader->FontRenderStyleForStrike(family, size, is_bold, is_italic,
                                             device_scale_factor,
                                             &font_render_style) ||
      font_render_style.is_null()) {
    LOG(ERROR) << "GetRenderStyleForStrike did not receive a response for "
                  "family and size: "
               << (family ? family : "<empty>") << ", " << size;
    return;
  }

  out->use_bitmaps = static_cast<char>(font_render_style->use_bitmaps);
  out->use_auto_hint = static_cast<char>(font_render_style->use_autohint);
  out->use_hinting = static_cast<char>(font_render_style->use_hinting);
  out->hint_style = font_render_style->hint_style;
  out->use_anti_alias = static_cast<char>(font_render_style->use_antialias);
  out->use_subpixel_rendering =
      static_cast<char>(font_render_style->use_subpixel_rendering);
  out->use_subpixel_positioning =
      static_cast<char>(font_render_style->use_subpixel_positioning);
}

}  // namespace content
