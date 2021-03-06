// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(_MSC_VER)
#ifdef __linux__
// Linux
#include <freetype/ftoutln.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#else
// Mac OS X
#include <ApplicationServices/ApplicationServices.h>  // g++ -framework Cocoa
#endif  // __linux__
#else
// Windows
// TODO(yusukes): Support Windows.
#endif  // _MSC_VER

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "opentype-sanitiser.h"
#include "ots-memory-stream.h"

namespace {

#if !defined(_MSC_VER)
#ifdef __linux__
// Linux
void LoadChar(FT_Face face, int pt, FT_ULong c) {
  FT_Matrix matrix;
  matrix.xx = matrix.yy = 1 << 16;
  matrix.xy = matrix.yx = 0 << 16;

  FT_Set_Char_Size(face, pt * (1 << 6), 0, 72, 0);
  FT_Set_Transform(face, &matrix, 0);
  FT_Load_Char(face, c, FT_LOAD_RENDER);
}

int OpenAndLoadChars(
    const char *file_name, uint8_t *trans_font, size_t trans_len) {
  FT_Library library;
  FT_Error error = FT_Init_FreeType(&library);
  if (error) {
    std::fprintf(stderr, "Failed to initialize FreeType2!\n");
    return 1;
  }

  FT_Face trans_face;
  error = FT_New_Memory_Face(library, trans_font, trans_len, 0, &trans_face);
  if (error) {
    std::fprintf(stderr,
                 "OK: FreeType2 couldn't open the transcoded font: %s\n",
                 file_name);
    return 0;
  }

  static const int kPts[] = {100, 20, 18, 16, 12, 10, 8};  // pt
  static const size_t kPtsLen = sizeof(kPts) / sizeof(kPts[0]);

  static const int kUnicodeRanges[] = {
    0x0020, 0x007E,  // Basic Latin (ASCII)
    0x00A1, 0x017F,  // Latin-1
    0x1100, 0x11FF,  // Hangul
    0x3040, 0x309F,  // Japanese HIRAGANA letters
    0x3130, 0x318F,  // Hangul
    0x4E00, 0x4F00,  // CJK Kanji/Hanja
    0xAC00, 0xAD00,  // Hangul
  };
  static const size_t kUnicodeRangesLen
      = sizeof(kUnicodeRanges) / sizeof(kUnicodeRanges[0]);

  for (size_t i = 0; i < kPtsLen; ++i) {
    for (size_t j = 0; j < kUnicodeRangesLen; j += 2) {
      for (int k = 0; k <= kUnicodeRanges[j + 1] - kUnicodeRanges[j]; ++k) {
        LoadChar(trans_face, kPts[i], kUnicodeRanges[j] + k);
      }
    }
  }

  std::fprintf(stderr, "OK: FreeType2 didn't crash: %s\n", file_name);
  return 0;
}
#else
// Mac OS X
int OpenAndLoadChars(
    const char *file_name, uint8_t *trans_font, size_t trans_len) {
  ATSFontContainerRef container_ref = 0;
  ATSFontActivateFromMemory(trans_font, trans_len, 3, kATSFontFormatUnspecified,
                            NULL, kATSOptionFlagsDefault, &container_ref);
  if (!container_ref) {
    std::fprintf(stderr,
                 "OK: font renderer couldn't open the transcoded font: %s\n",
                 file_name);
    return 0;
  }

  ItemCount count;
  ATSFontFindFromContainer(
      container_ref, kATSOptionFlagsDefault, 0, NULL, &count);
  if (!count) {
    std::fprintf(stderr,
                 "OK: font renderer couldn't open the transcoded font: %s\n",
                 file_name);
    return 0;
  }

  ATSFontRef ats_font_ref = 0;
  ATSFontFindFromContainer(
    container_ref, kATSOptionFlagsDefault, 1, &ats_font_ref, NULL);
  if (!ats_font_ref) {
    std::fprintf(stderr,
                 "OK: font renderer couldn't open the transcoded font: %s\n",
                 file_name);
    return 0;
  }

  CTFontRef ct_font_ref = CTFontCreateWithPlatformFont(ats_font_ref, 12,
                                                       NULL, NULL);
  if (!CTFontGetGlyphCount(ct_font_ref)) {
    std::fprintf(stderr,
                 "OK: font renderer couldn't open the transcoded font: %s\n",
                 file_name);
    return 0;
  }

  std::fprintf(stderr, "OK: font renderer didn't crash: %s\n", file_name);
  // TODO(yusukes): would be better to perform LoadChar() like Linux.
  return 0;
}
#endif  // __linux__
#else
// Windows
// TODO(yusukes): Support Windows.
#endif  // _MSC_VER

}  // namespace

int main(int argc, char **argv) {
  ots::DisableDebugOutput();  // turn off ERROR and WARNING outputs.

  if (argc != 2) {
    std::fprintf(stderr, "Usage: %s ttf_or_otf_filename\n", argv[0]);
    return 1;
  }

  // load the font to memory.
  const int fd = ::open(argv[1], O_RDONLY);
  if (fd < 0) {
    ::perror("open");
    return 1;
  }

  struct stat st;
  ::fstat(fd, &st);
  const off_t orig_len = st.st_size;

  uint8_t *orig_font = new uint8_t[orig_len];
  if (::read(fd, orig_font, orig_len) != orig_len) {
    std::fprintf(stderr, "Failed to read file!\n");
    return 1;
  }
  ::close(fd);

  // transcode the malicious font.
  static const size_t kBigPadLen = 1024 * 1024;  // 1MB
  uint8_t *trans_font = new uint8_t[orig_len + kBigPadLen];
  ots::MemoryStream output(trans_font, orig_len + kBigPadLen);

  bool result = ots::Process(&output, orig_font, orig_len);
  if (!result) {
    std::fprintf(stderr, "OK: the malicious font was filtered: %s\n", argv[1]);
    return 0;
  }
  const size_t trans_len = output.Tell();

  return OpenAndLoadChars(argv[1], trans_font, trans_len);
}
