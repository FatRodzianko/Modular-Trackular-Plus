#pragma once

#include <stddef.h>
#include <stdint.h>
#include "Themes.h"

class Platform;

class ThemeManager
{
  public:
    static constexpr int kMaxCustomThemes = 32;
    static constexpr int kMaxCustomFonts = 32;
    static constexpr int kMaxNameLength = 32;
    static constexpr int kMaxFilePathLength = 96;

    static void init(Platform* platform);
    static void scanThemesAndFonts();

    // Themes
    static uint8_t getThemeCount();
    static const char* const* getThemeNames();
    static bool applyTheme(uint8_t index);
    static const char* getCurrentThemeName();
    static uint8_t getCurrentThemeIndex() { return currentThemeIndex; }

    // Fonts
    static uint8_t getFontCount();
    static const char* const* getFontNames();
    static bool applyFont(uint8_t index);
    static const char* getCurrentFontName();
    static uint8_t getCurrentFontIndex() { return currentFontIndex; }

    // Parsing helpers
    static bool parseColor(const char* str, size_t len, uint32_t& outColor);
    static bool parseThemeJson(const char* jsonText, size_t jsonLen, Theme& outTheme);

  private:
    static Platform* platform;

    static uint8_t themeCount;
    static char themeNames[kMaxCustomThemes][kMaxNameLength];
    static const char* themeNamePointers[kMaxCustomThemes];
    static char themeFilePaths[kMaxCustomThemes][kMaxFilePathLength];
    static uint8_t currentThemeIndex;

    static uint8_t fontCount;
    static char fontNames[kMaxCustomFonts][kMaxNameLength];
    static const char* fontNamePointers[kMaxCustomFonts];
    static char fontFilePaths[kMaxCustomFonts][kMaxFilePathLength];
    static uint8_t currentFontIndex;

    static void ensureDefaultsInitialized();
    static void scanThemes();
    static void scanFonts();
};
