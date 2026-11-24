plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.valved.cam"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.valved.cam"
        minSdk = 29
        targetSdk = 35
        versionCode = 3
        versionName = "0.3"
    }

    // chave fixa de teste: assim cada versão nova instala por cima da anterior
    signingConfigs {
        create("teste") {
            storeFile = file("valvedcam-teste.jks")
            storePassword = "valvedcam"
            keyAlias = "valvedcam"
            keyPassword = "valvedcam"
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("teste")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    val camerax = "1.4.1"
    implementation("androidx.camera:camera-core:$camerax")
    implementation("androidx.camera:camera-camera2:$camerax")
    implementation("androidx.camera:camera-lifecycle:$camerax")
    implementation("androidx.camera:camera-video:$camerax")
    implementation("androidx.camera:camera-view:$camerax")
    implementation("androidx.activity:activity-ktx:1.9.3")
    implementation("androidx.core:core-ktx:1.13.1")
}
