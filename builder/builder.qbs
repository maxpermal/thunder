import qbs

Project {
    id: builder
    property stringList srcFiles: [
        "**/*.cpp",
        "**/*.h",
        "../develop/**/*.cpp",
        "../develop/**/*.h"
    ]

    property stringList incPaths: [
        "src",
        "../common",
        "../engine/includes",
        "../develop/managers/codemanager/include",
        "../develop/managers/projectmanager/include",
        "../develop/managers/assetmanager/include",
        "../develop/models/include",
        "../modules/renders/rendergl/includes",
        "../thirdparty/next/inc",
        "../thirdparty/next/inc/math",
        "../thirdparty/next/inc/core",
        "../thirdparty/physfs/inc",
        "../thirdparty/glfw/inc",
        "../thirdparty/fbx/inc",
        "../thirdparty/zlib/src",
        "../thirdparty/quazip/src"
    ]

    property stringList defines: {
        var result  = [
            "COMPANY_NAME=\"" + COMPANY_NAME + "\"",
            "EDITOR_NAME=\"" + EDITOR_NAME + "\"",
            "BUILDER_NAME=\"" + BUILDER_NAME + "\"",
            "SDK_VERSION=\"" + SDK_VERSION + "\"",
            "LAUNCHER_VERSION=\"" + LAUNCHER_VERSION + "\"",
            "YEAR=" + YEAR
        ];

        return result;
    }

    QtApplication {
        name: builder.BUILDER_NAME
        files: builder.srcFiles
        Depends { name: "cpp" }
        Depends { name: "zlib-editor" }
        Depends { name: "quazip-editor" }
        Depends { name: "next-editor" }
        Depends { name: "engine-editor" }
        Depends { name: "Qt"; submodules: ["core", "gui"]; }

        //builtByDefault: false

        cpp.defines: builder.defines
        cpp.includePaths: builder.incPaths
        cpp.libraryPaths: [
            "../thirdparty/fbx/lib"
        ]

        cpp.dynamicLibraries: [
            "fbxsdk-2012.1"
        ]

        Group {
            name: "Install " + builder.BUILDER_NAME
            fileTagsFilter: product.type
            qbs.install: true
            qbs.installDir: builder.BIN_PATH
            qbs.installPrefix: builder.PREFIX
        }
    }
}