plugins {
    id("com.android.application")
}

android {
    namespace = "io.github.gushu101.vcames"
    compileSdk = 35
    buildToolsVersion = "36.0.0"

    defaultConfig {
        applicationId = "io.github.gushu101.vcames"
        minSdk = 33
        targetSdk = 35
        versionCode = 10100
        versionName = "1.1.0"
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

    lint {
        abortOnError = true
        warningsAsErrors = true
        // API 35 is intentional: VCamES targets Android 13-15, even when a
        // newer SDK happens to be installed on the build host.
        disable += setOf("ExpiredTargetSdkVersion", "OldTargetApi")
    }
}
