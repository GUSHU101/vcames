import java.util.Properties

plugins {
    id("com.android.application")
}

val releaseProperties = Properties().apply {
    rootProject.file("version.properties").inputStream().use(::load)
}

android {
    namespace = "io.github.gushu101.vcames"
    compileSdk = 35
    buildToolsVersion = "36.0.0"

    defaultConfig {
        applicationId = "io.github.gushu101.vcames"
        minSdk = 30
        targetSdk = 35
        versionCode = releaseProperties.getProperty("versionCode").toInt()
        versionName = releaseProperties.getProperty("versionName")

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-Wall", "-Wextra", "-Werror")
            }
        }
    }

    flavorDimensions += "deployment"
    productFlavors {
        create("system") {
            dimension = "deployment"
        }
        create("root") {
            dimension = "deployment"
            versionNameSuffix = "-root"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    sourceSets.getByName("root").assets.srcDir(
        project.layout.buildDirectory.dir("generated/rootBridgeAssets")
    )

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    lint {
        abortOnError = true
        warningsAsErrors = true
        // Compile/target SDK can be newer than the deliberately narrow
        // runtime support matrix (Android 11-13 / API 30-33).
        disable += setOf(
            "ChromeOsAbiSupport",
            "ExpiredTargetSdkVersion",
            "OldTargetApi"
        )
    }
}
