--[[
This is for generating unix makefile.
I'm using premake5 alpha12 from http://premake.github.io/download.html#v5
(premake4 won't work, it doesn't support VS 2013+)
--]]

include("premake5.files.lua")

function regconf()
  filter "configurations:Debug"
    defines { "DEBUG" }
  filter {}

  filter "configurations:Release*"
    defines { "NDEBUG" }
    flags {
      "LinkTimeOptimization",
    }
    optimize "On"
  filter {}
end


workspace "SumatraPDF"
  toolset "gcc"
  configurations { "Debug", "Release" }
  platforms { "x64" }
  startproject "SumatraPDF"

  filter "platforms:x64"
     architecture "x86_64"
  filter {}

  warnings "Extra"

  location "unix"

  filter {"platforms:x64", "configurations:Release"}
    targetdir "out/rel64_unix"
  filter {"platforms:x64", "configurations:Debug"}
    targetdir "out/dbg64_unix"
  filter {}
  objdir "%{cfg.targetdir}/obj"

  symbols "On"

  -- https://github.com/premake/premake-core/wiki/flags
  flags {
    "MultiProcessorCompile",
    "StaticRuntime",
    -- "Unicode", TODO: breaks libdjuv?
  }

  -- expansion-to-defined reports as error commonly used pattern
  -- but not present in mac os x clang
  -- https://stackoverflow.com/questions/42074035/how-to-deal-with-clangs-3-9-wexpansion-to-defined-warning
  -- it's used in https://abseil.io/ headers so should be safe
  disablewarnings { "implicit-fallthrough" }
  flags { "FatalWarnings" }

  exceptionhandling "Off"
  rtti "Off"

  defines { }

  filter "configurations:Debug"
    defines { "DEBUG" }

  filter "configurations:Release*"
    defines { "NDEBUG" }
    optimize "On"
  filter {}

  include("premake5.opt.unix.lua")

  project "zlib"
    kind "StaticLib"
    language "C"
    regconf()
    defines {
      "_LARGEFILE64_SOURCE"
    }
    disablewarnings {}
    zlib_files()


  project "unrar"
    kind "StaticLib"
    language "C++"
    regconf()
    defines { "UNRAR", "RARDLL", "SILENT" }
    disablewarnings {}
    -- unrar uses exception handling in savepos.hpp but I don't want to enable it
    -- as it seems to infect the Sumatra binary as well (i.e. I see bad alloc exception
    -- being thrown)
    -- exceptionhandling "On"
    disablewarnings {} -- warning about using C++ exception handler without exceptions enabled

    includedirs { "ext/unrar" }
    unrar_files()


  project "libdjvu"
    kind "StaticLib"
    characterset ("MBCS")
    language "C++"
    regconf()
    defines {
      "NEED_JPEG_DECODER",
      "HAVE_PTHREAD=1",
      "HAVE_STDINT_H",
      "HAS_WCHAR",
      "HAS_MBSTATE",
      "UNIX",
      "POSIXTHREADS=1",
      "DDJVUAPI=/**/",
      "MINILISPAPI=/**/",
      "DEBUGLVL=0"
    }
    filter {"platforms:x32_asan or x64_asan"}
      defines { "DISABLE_MMX" }
    filter{}
    exceptionhandling "On"
    disablewarnings { "unused-parameter", "extra", "misleading-indentation" }
    includedirs { "ext/libjpeg-turbo" }
    libdjvu_files()


  project "wdl"
    kind "StaticLib"
    language "C++"
    regconf()
    includedirs  { "ext/WDL" }
    disablewarnings { "4018", "4100", "4244", "4505", "4456", "4457", "4245", "4505", "4701", "4706", "4996" }
    characterset "MBCS"
    wdl_files()


  project "unarrlib"
    kind "StaticLib"
    language "C"
    regconf()
    -- TODO: for bzip2, need BZ_NO_STDIO and BZ_DEBUG=0
    -- TODO: for lzma, need _7ZIP_PPMD_SUPPPORT
    defines { "HAVE_ZLIB", "HAVE_BZIP2", "HAVE_7Z", "BZ_NO_STDIO", "_7ZIP_PPMD_SUPPPORT" }
    -- TODO: most of these warnings are due to bzip2 and lzma
    disablewarnings { "4100", "4244", "4267", "4456", "4457", "4996" }
    includedirs { "ext/zlib", "ext/bzip2", "ext/lzma/C" }
    unarr_files()


  project "jbig2dec"
    kind "StaticLib"
    language "C"
    regconf()
    defines { "_CRT_SECURE_NO_WARNINGS", "HAVE_STRING_H=1", "JBIG_NO_MEMENTO" }
    disablewarnings { "4018", "4100", "4146", "4244", "4267", "4456", "4701" }
    includedirs { "ext/jbig2dec" }
    jbig2dec_files()


  project "openjpeg"
    kind "StaticLib"
    language "C"
    regconf()
    disablewarnings { "4100", "4244", "4310" }
    -- openjpeg has opj_config_private.h for such over-rides
    -- but we can't change it because we bring openjpeg as submodule
    -- and we can't provide our own in a different directory because
    -- msvc will include the one in ext/openjpeg/src/lib/openjp2 first
    -- because #include "opj_config_private.h" searches current directory first
    defines { "_CRT_SECURE_NO_WARNINGS", "USE_JPIP", "OPJ_STATIC", "OPJ_EXPORTS" }
    openjpeg_files()


  project "libwebp"
    kind "StaticLib"
    language "C"
    regconf()
    disablewarnings { "4204", "4244", "4057", "4245", "4310" }
    includedirs { "ext/libwebp" }
    libwebp_files()


  project "libjpeg-turbo"
    kind "StaticLib"
    language "C"
    regconf()
    defines { "_CRT_SECURE_NO_WARNINGS" }
    disablewarnings { "4018", "4100", "4244", "4245" }
    includedirs { "ext/libjpeg-turbo", "ext/libjpeg-turbo/simd" }

    -- nasm.exe -I .\ext\libjpeg-turbo\simd\
    -- -I .\ext\libjpeg-turbo\win\ -f win32
    -- -o .\obj-rel\jpegturbo\jsimdcpu.obj
    -- .\ext\libjpeg-turbo\simd\jsimdcpu.asm
    filter {'files:**.asm', 'platforms:x32 or x32_asan'}
       buildmessage '%{file.relpath}'
       buildoutputs { '%{cfg.objdir}/%{file.basename}.obj' }
       buildcommands {
          '..\\bin\\nasm.exe -f win32 -I ../ext/libjpeg-turbo/simd/ -I ../ext/libjpeg-turbo/win/ -o "%{cfg.objdir}/%{file.basename}.obj" "%{file.relpath}"'
       }
    filter {}

    filter {'files:**.asm', 'platforms:x64 or x64_asan or x64_ramicro'}
      buildmessage '%{file.relpath}'
      buildoutputs { '%{cfg.objdir}/%{file.basename}.obj' }
      buildcommands {
        '..\\bin\\nasm.exe -f win64 -D__x86_64__ -DWIN64 -DMSVC -I ../ext/libjpeg-turbo/simd/ -I ../ext/libjpeg-turbo/win/ -o "%{cfg.objdir}/%{file.basename}.obj" "%{file.relpath}"'
      }
    filter {}
    libjpeg_turbo_files()


  project "freetype"
    kind "StaticLib"
    language "C"
    regconf()
    defines {
      "FT2_BUILD_LIBRARY",
      "FT_CONFIG_MODULES_H=\"slimftmodules.h\"",
      "FT_CONFIG_OPTIONS_H=\"slimftoptions.h\"",
    }
    disablewarnings { "4018", "4244", "4267", "4312", "4996" }
    includedirs { "mupdf/scripts/freetype", "ext/freetype/include" }
    freetype_files()

  project "lcms2"
    kind "StaticLib"
    language "C"
    regconf()
    disablewarnings { "4100" }
    includedirs { "ext/lcms2/include" }
    lcms2_files()


  project "harfbuzz"
    kind "StaticLib"
    language "C"
    regconf()
    includedirs { "ext/harfbuzz/src/hb-ucdn", "mupdf/scripts/freetype", "ext/freetype/include" }
    defines {
      "_CRT_SECURE_NO_WARNINGS",
      "HAVE_FALLBACK=1",
      "HAVE_OT",
      "HAVE_UCDN",
      "HAVE_FREETYPE",
      "HB_NO_MT",
      "hb_malloc_impl=fz_hb_malloc",
      "hb_calloc_impl=fz_hb_calloc",
      "hb_realloc_impl=fz_hb_realloc",
      "hb_free_impl=fz_hb_free"
    }
    disablewarnings { "4100", "4146", "4244", "4245", "4267", "4456", "4457", "4459", "4701", "4702", "4706" }
    harfbuzz_files()


  project "mujs"
    kind "StaticLib"
    language "C"
    regconf()
    includedirs { "ext/mujs" }
    disablewarnings { "4090", "4100", "4310", "4702", "4706" }
    files { "ext/mujs/one.c", "ext/mujs/mujs.h" }


  project "chm"
    kind "StaticLib"
    language "C"
    regconf()
    defines { "UNICODE", "_UNICODE", "PPC_BSTR"}
    disablewarnings { "4018", "4057", "4189", "4244", "4267", "4295", "4701", "4706", "4996" }
    files { "ext/CHMLib/src/chm_lib.c", "ext/CHMLib/src/lzx.c" }


  project "engines"
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    regconf()
    disablewarnings {
      "4018", "4057", "4100", "4189", "4244", "4267", "4295", "4457",
      "4701", "4706", "4819", "4838"
    }
    includedirs { "src", "src/wingui" }
    includedirs { "ext/synctex", "ext/libdjvu", "ext/CHMLib/src", "ext/zlib", "mupdf/include" }
    engines_files()
    links { "chm" }


  project "gumbo"
    kind "StaticLib"
    language "C"
    regconf()
    disablewarnings { "4018", "4100", "4132", "4204", "4244", "4245", "4267", 
    "4305", "4306", "4389", "4456", "4701" }
    includedirs { "ext/gumbo-parser/include", "ext/gumbo-parser/visualc/include" }
    gumbo_files()

  project "mupdf"
    kind "StaticLib"
    language "C"
    regconf()
    -- for openjpeg, OPJ_STATIC is alrady defined in load-jpx.c
    -- so we can't double-define it
    defines { "USE_JPIP", "OPJ_EXPORTS", "HAVE_LCMS2MT=1" }
    defines { "OPJ_STATIC", "SHARE_JPEG" }
    -- this defines which fonts are to be excluded from being included directly
    -- we exclude the very big cjk fonts
    defines { "TOFU", "TOFU_CJK_LANG" }

    disablewarnings {
      "4005", "4018", "4100", "4115", "4130", "4204", "4206", "4245", "4267", "4295",
      "4305", "4389", "4456", "4703", "4706"
    }
    -- force including mupdf/scripts/openjpeg/opj_config_private.h
    -- with our build over-rides

    includedirs {
      "mupdf/include",
      "mupdf/generated",
      "ext/jbig2dec",
      "ext/libjpeg-turbo",
      "ext/openjpeg/src/lib/openjp2",
      "ext/zlib",
      "mupdf/scripts/freetype",
      "ext/freetype/include",
      "ext/mujs",
      "ext/harfbuzz/src",
      "ext/lcms2/include",
      "ext/gumbo-parser/src",
  }
    -- .\ext\..\bin\nasm.exe -I .\mupdf\ -f win32 -o .\obj-rel\mupdf\font_base14.obj
    -- .\mupdf\font_base14.asm
    filter {'files:**.asm', 'platforms:x32 or x32_asan'}
       buildmessage 'Compiling %{file.relpath}'
       buildoutputs { '%{cfg.objdir}/%{file.basename}.obj' }
       buildcommands {
          '..\\bin\\nasm.exe -f win32 -I ../mupdf/ -o "%{cfg.objdir}/%{file.basename}.obj" "%{file.relpath}"'
       }
    filter {}

    filter {'files:**.asm', 'platforms:x64 or x64_asan or x64_ramicro'}
      buildmessage 'Compiling %{file.relpath}'
      buildoutputs { '%{cfg.objdir}/%{file.basename}.obj' }
      buildcommands {
        '..\\bin\\nasm.exe -f win64 -DWIN64 -I ../mupdf/ -o "%{cfg.objdir}/%{file.basename}.obj" "%{file.relpath}"'
      }
    filter {}

    mupdf_files()
    links { "zlib", "freetype", "libjpeg-turbo", "jbig2dec", "openjpeg", "lcms2", "harfbuzz", "mujs", "gumbo" }

  -- regular build with distinct debug / release builds
  project "libmupdf-reg"
    kind "SharedLib"
    language "C"
    regconf()
    disablewarnings { "4206", "4702" }
    defines { "FZ_ENABLE_SVG" }
    -- premake has logic in vs2010_vcxproj.lua that only sets PlatformToolset
    -- if there is a c/c++ file, so we add a no-op cpp file to force This logic
    files { "src/libmupdf.rc", "src/no_op_for_premake.cpp" }
    implibname "libmupdf"
    -- TODO: is thre a better way to do it?
    -- TODO: only for windows
    linkoptions { "/DEF:..\\src\\libmupdf.def", "-IGNORE:4702" }
    links { "mupdf", "libdjvu", "libwebp", "unarrlib" }
    links {
      "advapi32", "kernel32", "user32", "gdi32", "comdlg32",
      "shell32", "windowscodecs", "comctl32", "msimg32",
      "winspool", "wininet", "urlmon", "gdiplus", "ole32",
      "oleAut32", "shlwapi", "version", "crypt32"
    }

  -- should be called libmupdf-opt but we don't want that .dll name
  project "libmupdf"
    kind "SharedLib"
    language "C"
    optconf()
    disablewarnings { "4206", "4702" }
    defines { "FZ_ENABLE_SVG" }

    -- premake has logic in vs2010_vcxproj.lua that only sets PlatformToolset
    -- if there is a c/c++ file, so we add a no-op cpp file to force This logic
    files { "src/libmupdf.rc", "src/no_op_for_premake.cpp" }
    implibname "libmupdf"
    -- TODO: is there a better way to do it?
    -- TODO: only for windows
    linkoptions { "/DEF:..\\src\\libmupdf.def", "-IGNORE:4702" }
    links { "mupdf-opt", "libdjvu-opt", "unarrlib-opt", "libwebp-opt" }
    links {
      "advapi32", "kernel32", "user32", "gdi32", "comdlg32",
      "shell32", "windowscodecs", "comctl32", "msimg32",
      "winspool", "wininet", "urlmon", "gdiplus", "ole32",
      "oleAut32", "shlwapi", "version", "crypt32"
    }

  project "utils"
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    regconf()
    filter "configurations:ReleaseAnalyze"
      -- TODO: somehow /analyze- is default which creates warning about
      -- over-ride from cl.exe. Don't know how to disable the warning
      buildoptions { "/analyze" }
      disablewarnings { "28125", "28252", "28253" }
    filter {}

    -- QITABENT in shlwapi.h has incorrect definition and causes 4838
    disablewarnings { "4100", "4267", "4457", "4838" }
    includedirs { "src", "ext/zlib", "ext/lzma/C" }
    includedirs { "ext/libwebp/src", "ext/unarr", "mupdf/include" }
    utils_files()

  -- a single static executable
  project "SumatraPDF"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++latest"
    regconf()
    entrypoint "WinMainCRTStartup"
    flags { "NoManifest" }
    includedirs { "src", "mupdf/include", "ext/WDL" }

    synctex_files()
    mui_files()
    uia_files()
    sumatrapdf_files()

    defines { "_CRT_SECURE_NO_WARNINGS" }
    defines { "DISABLE_DOCUMENT_RESTRICTIONS" }

    filter "configurations:ReleaseAnalyze"
      -- TODO: somehow /analyze- is default which creates warning about
      -- over-ride from cl.exe. Don't know how to disable the warning
      buildoptions { "/analyze" }
      disablewarnings { "28125", "28252", "28253" }
    filter {}

    -- for synctex
    disablewarnings { "4100", "4244", "4267", "4702", "4706" }
    includedirs { "ext/zlib", "ext/synctex" }

    -- for uia
    disablewarnings { "4302", "4311", "4838" }

    -- for wdl
    disablewarnings { "4505" }

    links {
      "engines", "libdjvu",  "libwebp", "mupdf", "unarrlib", "utils", "unrar", "wdl"
    }
    links {
      "comctl32", "delayimp", "gdiplus", "msimg32", "shlwapi", "urlmon",
      "version", "windowscodecs", "wininet", "uiautomationcore.lib"
    }
    -- configAsan()
    -- this is to prevent dll hijacking
    linkoptions { "/DELAYLOAD:gdiplus.dll /DELAYLOAD:msimg32.dll /DELAYLOAD:shlwapi.dll" }
    linkoptions { "/DELAYLOAD:urlmon.dll /DELAYLOAD:version.dll /DELAYLOAD:wininet.dll" }
    linkoptions { "/DELAYLOAD:uiautomationcore.dll" }


