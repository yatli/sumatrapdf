-- those are versions of projects where Debug configurations are compiled
-- with full otpimizations. This is done for mupdf etc., which we assume
-- are stable libraries so we don't need debug support for them
-- this only applies to libmupdf.dll build because in static build I can't
-- (easily, with premake) mix optimized and non-optimized compilation

function optconf()
    optimize "On"
    undefines { "DEBUG" }
    defines { "NDEBUG" }
end

project "libwebp-opt"
    kind "StaticLib"
    language "C"
    optconf()
    disablewarnings { "4204", "4244", "4057", "4245", "4310" }
    includedirs { "ext/libwebp" }
    libwebp_files()


project "libdjvu-opt"
    kind "StaticLib"
    characterset ("MBCS")
    language "C++"
    optconf()
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


project "unarrlib-opt"
    kind "StaticLib"
    language "C"
    optconf()

    -- TODO: for bzip2, need BZ_NO_STDIO and BZ_DEBUG=0
    -- TODO: for lzma, need _7ZIP_PPMD_SUPPPORT
    defines { "HAVE_ZLIB", "HAVE_BZIP2", "HAVE_7Z", "BZ_NO_STDIO", "_7ZIP_PPMD_SUPPPORT", 
              "_FILE_OFFSET_BITS=64" ,"_UNIX" }
    -- TODO: most of these warnings are due to bzip2 and lzma
    disablewarnings { "unused-but-set-variable", "int-conversion", "unused-parameter", "sign-compare", "type-limits" }
    includedirs { "ext/zlib", "ext/bzip2", "ext/lzma/C" }
    files {
      "ext/unarr/common/*",
      "ext/unarr/rar/*",
      "ext/unarr/zip/*",
      "ext/unarr/tar/*",
      "ext/unarr/_7z/*",

      "ext/bzip2/bzip_all.c",
    }
    unarrr_lzmasdk_files()
    -- doesn't work
    -- unarr_lzma_files()


project "jbig2dec-opt"
    kind "StaticLib"
    language "C"
    optconf()

    defines { "HAVE_STRING_H=1", "JBIG_NO_MEMENTO" }
    disablewarnings { "unused-parameter", "shift-negative-value" }
    includedirs { "ext/jbig2dec" }
    jbig2dec_files()


project "openjpeg-opt"
    kind "StaticLib"
    language "C"
    optconf()

    disablewarnings { "shift-count-overflow" }
    -- openjpeg has opj_config_private.h for such over-rides
    -- but we can't change it because we bring openjpeg as submodule
    -- and we can't provide our own in a different directory because
    -- msvc will include the one in ext/openjpeg/src/lib/openjp2 first
    -- because #include "opj_config_private.h" searches current directory first
    defines { "OPJ_HAVE_STDINT_H", "OPJ_HAVE_INTTYPES_H", "USE_JPIP", "OPJ_STATIC", "OPJ_EXPORTS" }
    openjpeg_files()

project "lcms2-opt"
    kind "StaticLib"
    language "C"
    optconf()

    disablewarnings { "unused-parameter", "strict-aliasing", "type-limits", "unused-but-set-variable" }
    includedirs { "ext/lcms2/include" }
    lcms2_files()

project "harfbuzz-opt"
    kind "StaticLib"
    language "C"
    optconf()

    includedirs { "ext/harfbuzz/src/hb-ucdn", "mupdf/scripts/freetype", "ext/freetype/include" }
    defines {
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
    disablewarnings { }
    harfbuzz_files()

project "mujs-opt"
    kind "StaticLib"
    language "C"
    optconf()

    includedirs { "ext/mujs" }
    disablewarnings { "clobbered", "strict-overflow", "unused-parameter" }
    files { "ext/mujs/one.c", "ext/mujs/mujs.h" }

project "freetype-opt"
    kind "StaticLib"
    language "C"
    optconf()

    defines {
        "FT2_BUILD_LIBRARY",
        "FT_CONFIG_MODULES_H=\"slimftmodules.h\"",
        "FT_CONFIG_OPTIONS_H=\"slimftoptions.h\"",
    }
    disablewarnings { "unused-variable" }
    includedirs { "mupdf/scripts/freetype", "ext/freetype/include" }
    freetype_files()

project "libjpeg-turbo-opt"
    kind "StaticLib"
    language "C"
    optconf()

    defines { }
    disablewarnings { "shift-negative-value", "unused-parameter", "attributes" }
    includedirs { "ext/libjpeg-turbo", "ext/libjpeg-turbo/simd" }

    -- nasm.exe -I .\ext\libjpeg-turbo\simd\
    -- -I .\ext\libjpeg-turbo\win\ -f win32
    -- -o .\obj-rel\jpegturbo\jsimdcpu.obj
    -- .\ext\libjpeg-turbo\simd\jsimdcpu.asm

    filter {'files:**.asm', 'platforms:x64 or x64_asan or x64_ramicro'}
        buildmessage '%{file.relpath}'
        buildoutputs { '%{cfg.objdir}/%{file.basename}.o' }
        buildcommands {
        'nasm -f elf64 -D__x86_64__ -I ../ext/libjpeg-turbo/simd/ -I ../ext/libjpeg-turbo/win/ -o "%{cfg.objdir}/%{file.basename}.o" "%{file.relpath}"'
        }
    filter {}
    libjpeg_turbo_files()

project "zlib-opt"
    kind "StaticLib"
    language "C"
    optconf()
    defines {
      "_LARGEFILE64_SOURCE"
    }

    disablewarnings { }
    zlib_files()

project "gumbo-opt"
    kind "StaticLib"
    language "C"
    optconf()
    disablewarnings { "unused-variable", "unused-parameter", "old-style-declaration" }
    includedirs { "ext/gumbo-parser/include" }
    gumbo_files()

project "mupdf-opt"
    kind "StaticLib"
    language "C"
    optconf()

    -- for openjpeg, OPJ_STATIC is alrady defined in load-jpx.c
    -- so we can't double-define it
    defines { "OPJ_HAVE_STDINT_H", "OPJ_HAVE_INTTYPES_H" }
    defines { "USE_JPIP", "OPJ_EXPORTS", "HAVE_LCMS2MT=1" }
    defines { "OPJ_STATIC", "SHARE_JPEG" }
    -- this defines which fonts are to be excluded from being included directly
    -- we exclude the very big cjk fonts
    defines { "TOFU", "TOFU_CJK_LANG" }
    disablewarnings {
      "unused-parameter", "clobbered", "missing-field-initializers", "old-style-declaration", "maybe-uninitialized", "misleading-indentation",
      "shift-negative-value", "unused-result", "unused-but-set-variable"
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

    filter {'files:**.asm', 'platforms:x64 or x64_asan or x64_ramicro'}
    buildmessage 'Compiling %{file.relpath}'
    buildoutputs { '%{cfg.objdir}/%{file.basename}.o' }
    buildcommands {
        'nasm -f elf64 -I ../mupdf/ -o "%{cfg.objdir}/%{file.basename}.o" "%{file.relpath}"'
    }
    filter {}
    --[[ files {
      "mupdf/font_base14.asm",
    }
    --]]

    -- files { "ext/mupdf_load_system_font.c" }

    filter {"platforms:x64 or x64_asan or x64_ramicro"}
      files {
        "mupdf/fonts_64.asm",
      }
    filter {}

    filter {"platforms:x32 or x32_asan"}
      files {
        "mupdf/fonts_32.asm",
      }
    filter {}

    files_in_dir("mupdf/source/cbz", {
      "mucbz.c",
      "muimg.c",
    })

    files { "mupdf/source/fitz/*.h" }
    files_in_dir("mupdf/source/fitz", {
      "archive.c",
      "bbox-device.c",
      "bidi.c",
      "bidi-std.c",
      "bitmap.c",
      "buffer.c",
      "color-fast.c",
      "color-icc-create.c",
      "color-lcms.c",
      "colorspace.c",
      "compress.c",
      "compressed-buffer.c",
      "context.c",
      "crypt-aes.c",
      "crypt-arc4.c",
      "crypt-md5.c",
      "crypt-sha2.c",
      "device.c",
      "directory.c",
      "document.c",
      "document-all.c",
      "draw-affine.c",
      "draw-blend.c",
      "draw-device.c",
      "draw-edge.c",
      "draw-edgebuffer.c",
      "draw-glyph.c",
      "draw-mesh.c",
      "draw-paint.c",
      "draw-path.c",
      "draw-rasterize.c",
      "draw-scale-simple.c",
      "draw-unpack.c",
      "encode-basic.c",
      "encode-fax.c",
      "encodings.c",
      "error.c",
      "filter-basic.c",
      "filter-dct.c",
      "filter-fax.c",
      "filter-flate.c",
      "filter-jbig2.c",
      "filter-leech.c",
      "filter-lzw.c",
      "filter-predict.c",
      "filter-sgi.c",
      "filter-thunder.c",
      "font.c",
      "ftoa.c",
      "geometry.c",
      "getopt.c",
      "glyph.c",
      "halftone.c",
      "harfbuzz.c",
      "hash.c",
      "image.c",
      "jmemcust.c",
      "link.c",
      "list-device.c",
      "load-bmp.c",
      "load-gif.c",
      "load-jbig2.c",
      "load-jpeg.c",
      "load-jpx.c",
      "load-jxr.c",
      "load-png.c",
      "load-pnm.c",
      "load-tiff.c",
      "log.c",
      "memento.c",
      "memory.c",
      "noto.c",
      "outline.c",
      "output.c",
      "output-cbz.c",
      "output-pcl.c",
      "output-pclm.c",
      "output-png.c",
      "output-pnm.c",
      "output-ps.c",
      "output-psd.c",
      "output-pwg.c",
      "output-svg.c",
      "path.c",
      "pixmap.c",
      "pool.c",
      "printf.c",
      "random.c",
      "separation.c",
      "shade.c",
      "stext-device.c",
      "stext-output.c",
      "stext-search.c",
      "store.c",
      "stream-open.c",
      "stream-read.c",
      "string.c",
      "strtof.c",
      "svg-device.c",
      "test-device.c",
      "text.c",
      "time.c",
      "trace-device.c",
      "track-usage.c",
      "transition.c",
      "tree.c",
      "ucdn.c",
      "untar.c",
      "unzip.c",
      "util.c",
      "writer.c",
      "xml.c",
      "zip.c",
    })

    files_in_dir("mupdf/source/html", {
      "css-apply.c",
      "css-parse.c",
      "epub-doc.c",
      "html-doc.c",
      "html-font.c",
      "html-layout.c",
      "html-outline.c",
      "html-parse.c",
    })

    files_in_dir("mupdf/source/pdf", {
      "pdf-annot.c",
      "pdf-appearance.c",
      "pdf-clean.c",
      "pdf-clean-file.c",
      "pdf-cmap.c",
      "pdf-cmap-load.c",
      "pdf-cmap-parse.c",
      "pdf-colorspace.c",
      "pdf-crypt.c",
      "pdf-device.c",
      "pdf-event.c",
      "pdf-font.c",
      "pdf-font-add.c",
      "pdf-form.c",
      "pdf-function.c",
      "pdf-graft.c",
      "pdf-image.c",
      "pdf-interpret.c",
      "pdf-js.c",
      "pdf-layer.c",
      "pdf-lex.c",
      "pdf-link.c",
      "pdf-metrics.c",
      "pdf-nametree.c",
      "pdf-object.c",
      "pdf-op-buffer.c",
      "pdf-op-filter.c",
      "pdf-op-run.c",
      "pdf-outline.c",
      "pdf-page.c",
      "pdf-parse.c",
      "pdf-pattern.c",
      "pdf-repair.c",
      "pdf-resources.c",
      "pdf-run.c",
      "pdf-shade.c",
      "pdf-signature.c",
      "pdf-store.c",
      "pdf-stream.c",
      "pdf-type3.c",
      "pdf-unicode.c",
      "pdf-util.c",
      "pdf-write.c",
      "pdf-xobject.c",
      "pdf-xref.c",
    })

    files_in_dir("mupdf/source/svg", {
      "svg-color.c",
      "svg-doc.c",
      "svg-parse.c",
      "svg-run.c",
    })

    files_in_dir("mupdf/source/xps", {
      "xps-common.c",
      "xps-doc.c",
      "xps-glyphs.c",
      "xps-gradient.c",
      "xps-image.c",
      "xps-link.c",
      "xps-outline.c",
      "xps-path.c",
      "xps-resource.c",
      "xps-tile.c",
      "xps-util.c",
      "xps-zip.c",
    })
    files {
      "mupdf/include/mupdf/fitz/*.h",
      "mupdf/include/mupdf/helpers/*.h",
      "mupdf/include/mupdf/pdf/*.h",
      "mupdf/include/mupdf/*.h"
    }

    links { "zlib-opt", "libjpeg-turbo-opt", "freetype-opt", "jbig2dec-opt", "openjpeg-opt", "lcms2-opt", "harfbuzz-opt", "mujs-opt", "gumbo-opt" }
