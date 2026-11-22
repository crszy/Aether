#pragma once
#if defined(__cplusplus) && defined(_WIN32)
#ifdef CAE_BUILD_CORE
#define CAE_CORE_API __declspec(dllexport)
#else
#define CAE_CORE_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_SETTINGS
#define CAE_SETTINGS_API __declspec(dllexport)
#else
#define CAE_SETTINGS_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_COMPONENTS
#define CAE_COMPONENTS_API __declspec(dllexport)
#else
#define CAE_COMPONENTS_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_CONFIG
#define CAE_CONFIG_API __declspec(dllexport)
#else
#define CAE_CONFIG_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_STATE
#define CAE_STATE_API __declspec(dllexport)
#else
#define CAE_STATE_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_MODELS
#define CAE_MODELS_API __declspec(dllexport)
#else
#define CAE_MODELS_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_SERVICES
#define CAE_SERVICES_API __declspec(dllexport)
#else
#define CAE_SERVICES_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_BLOBS
#define CAE_BLOBS_API __declspec(dllexport)
#else
#define CAE_BLOBS_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_IMAGES
#define CAE_IMAGES_API __declspec(dllexport)
#else
#define CAE_IMAGES_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_I18N
#define CAE_I18N_API __declspec(dllexport)
#else
#define CAE_I18N_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_MULTIMEDIA
#define CAE_MULTIMEDIA_API __declspec(dllexport)
#else
#define CAE_MULTIMEDIA_API __declspec(dllimport)
#endif
#ifdef CAE_BUILD_SHADERS
#define CAE_SHADERS_API __declspec(dllexport)
#else
#define CAE_SHADERS_API __declspec(dllimport)
#endif
#else
#define CAE_CORE_API
#define CAE_SETTINGS_API
#define CAE_COMPONENTS_API
#define CAE_CONFIG_API
#define CAE_STATE_API
#define CAE_MODELS_API
#define CAE_SERVICES_API
#define CAE_BLOBS_API
#define CAE_IMAGES_API
#define CAE_I18N_API
#define CAE_MULTIMEDIA_API
#define CAE_SHADERS_API
#endif
#define CAE_MACRO_API
