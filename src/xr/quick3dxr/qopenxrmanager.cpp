// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include "qopenxrmanager_p.h"
#include "qopenxrinputmanager_p.h"
#include <openxr/openxr_reflection.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>

#include <rhi/qrhi.h>

#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/QQuickRenderControl>
#include <QtQuick/QQuickRenderTarget>
#include <QtQuick/QQuickItem>

#include <QtQuick3D/private/qquick3dnode_p.h>
#include <QtQuick3D/private/qquick3dviewport_p.h>

#include "qopenxrcamera_p.h"

#ifdef XR_USE_GRAPHICS_API_VULKAN
# include "qopenxrgraphics_vulkan_p.h"
#endif

#ifdef XR_USE_GRAPHICS_API_D3D11
# include "qopenxrgraphics_d3d11_p.h"
#endif

#ifdef XR_USE_GRAPHICS_API_D3D12
# include "qopenxrgraphics_d3d12_p.h"
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL
# include "qopenxrgraphics_opengl_p.h"
#endif

#ifdef XR_USE_PLATFORM_ANDROID
# include <QtCore/qnativeinterface.h>
# include <QtCore/QJniEnvironment>
# include <QtCore/QJniObject>
# ifdef XR_USE_GRAPHICS_API_OPENGL_ES
#  include "qopenxrgraphics_opengles_p.h"
# endif // XR_USE_GRAPHICS_API_OPENGL_ES
#endif // XR_USE_PLATFORM_ANDROID

#include "qopenxrhelpers_p.h"
#include "qopenxrorigin_p.h"

#include "qopenxrspaceextension_p.h"

QT_BEGIN_NAMESPACE

// Macro to generate stringify functions for OpenXR enumerations based data provided in openxr_reflection.h
#define ENUM_CASE_STR(name, val) case name: return #name;
#define MAKE_TO_STRING_FUNC(enumType)                  \
    static inline const char* to_string(enumType e) {         \
    switch (e) {                                   \
    XR_LIST_ENUM_##enumType(ENUM_CASE_STR)     \
    default: return "Unknown " #enumType;      \
    }                                              \
    }

MAKE_TO_STRING_FUNC(XrReferenceSpaceType);
MAKE_TO_STRING_FUNC(XrViewConfigurationType);
MAKE_TO_STRING_FUNC(XrEnvironmentBlendMode);
MAKE_TO_STRING_FUNC(XrSessionState);
MAKE_TO_STRING_FUNC(XrResult);
//MAKE_TO_STRING_FUNC(XrFormFactor);

QOpenXRManager::QOpenXRManager(QObject *parent)
    : QObject(parent)
{

}

QOpenXRManager::~QOpenXRManager()
{
    teardown();

    // maintain the correct order
    delete m_vrViewport;
    delete m_quickWindow;
    delete m_renderControl;
    delete m_animationDriver;
    delete m_graphics; // last, with Vulkan this may own the VkInstance
}

namespace  {
bool isExtensionSupported(const char *extensionName, const QVector<XrExtensionProperties> &instanceExtensionProperties, uint32_t *extensionVersion = nullptr)
{
    for (const auto &extensionProperty : instanceExtensionProperties) {
        if (!strcmp(extensionName, extensionProperty.extensionName)) {
            if (extensionVersion)
                *extensionVersion = extensionProperty.extensionVersion;
            return true;
        }
    }
    return false;
}

bool isApiLayerSupported(const char *layerName, const QVector<XrApiLayerProperties> &apiLayerProperties)
{
    for (const auto &prop : apiLayerProperties) {
        if (!strcmp(layerName, prop.layerName))
            return true;
    }
    return false;
}

// OpenXR's debug messenger stuff is a carbon copy of the Vulkan one, hence we
// replicate the same behavior on Qt side as well, i.e. route by default
// everything to qDebug. Filtering or further control (that is supported with
// the C++ APIS in the QVulkan* stuff) is not provided here for now.
#ifdef XR_EXT_debug_utils
XRAPI_ATTR XrBool32 XRAPI_CALL defaultDebugCallbackFunc(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
                                                        XrDebugUtilsMessageTypeFlagsEXT messageType,
                                                        const XrDebugUtilsMessengerCallbackDataEXT *callbackData,
                                                        void *userData)
{
    Q_UNUSED(messageSeverity);
    Q_UNUSED(messageType);
    QOpenXRManager *self = static_cast<QOpenXRManager *>(userData);
    qDebug("xrDebug [QOpenXRManager %p] %s", self, callbackData->message);
    return XR_FALSE;
}
#endif

} // namespace

void QOpenXRManager::setErrorString(XrResult result, const char *callName)
{
    m_errorString = tr("%1 for runtime %2 %3 failed with %4.")
                        .arg(QLatin1StringView(callName),
                             m_runtimeName,
                             m_runtimeVersion.toString(),
                             OpenXRHelpers::getXrResultAsString(result, m_instance));
    if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) // this is very common
        m_errorString += tr("\nThe OpenXR runtime has no connection to the headset; check if connection is active and functional.");
}

bool QOpenXRManager::initialize()
{
    m_errorString.clear();

    // This, meaning constructing the QGraphicsFrameCapture if we'll want it,
    // must be done as early as possible, before initalizing graphics. In fact
    // in hybrid apps it might be too late at this point if Qt Quick (so someone
    // outside our control) has initialized graphics which then makes
    // RenderDoc's hooking mechanisms disfunctional.
    if (qEnvironmentVariableIntValue("QT_QUICK3D_XR_FRAME_CAPTURE")) {
#if QT_CONFIG(graphicsframecapture)
        m_frameCapture.reset(new QGraphicsFrameCapture);
#else
        qWarning("Quick 3D XR: Frame capture was requested, but Qt is built without QGraphicsFrameCapture");
#endif
    }

#ifdef XR_USE_PLATFORM_ANDROID
    // Initialize the Loader
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
    xrGetInstanceProcAddr(
        XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
    if (xrInitializeLoaderKHR != NULL) {
        JavaVM *javaVM = QJniEnvironment::javaVM();
        m_androidActivity = QNativeInterface::QAndroidApplication::context();

        XrLoaderInitInfoAndroidKHR loaderInitializeInfoAndroid;
        memset(&loaderInitializeInfoAndroid, 0, sizeof(loaderInitializeInfoAndroid));
        loaderInitializeInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
        loaderInitializeInfoAndroid.next = NULL;
        loaderInitializeInfoAndroid.applicationVM = javaVM;
        loaderInitializeInfoAndroid.applicationContext = m_androidActivity.object();
        XrResult xrResult = xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitializeInfoAndroid);
        if (xrResult != XR_SUCCESS) {
            qWarning("Failed to initialize OpenXR Loader: %s", to_string(xrResult));
            return false;
        }
    }
#endif

    // Decide if we do multiview rendering.
    m_multiviewRendering = qEnvironmentVariableIntValue("QT_QUICK3D_XR_MULTIVIEW");
    qDebug("Quick3D XR: multiview rendering requested = %s", m_multiviewRendering ? "yes" : "no");

    // Init the Graphics Backend
    auto graphicsAPI = QQuickWindow::graphicsApi();

    m_graphics = nullptr;
#ifdef XR_USE_GRAPHICS_API_VULKAN
    if (graphicsAPI == QSGRendererInterface::Vulkan)
        m_graphics = new QOpenXRGraphicsVulkan;
#endif
#ifdef XR_USE_GRAPHICS_API_D3D11
    if (graphicsAPI == QSGRendererInterface::Direct3D11)
        m_graphics = new QOpenXRGraphicsD3D11;
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
    if (graphicsAPI == QSGRendererInterface::Direct3D12)
        m_graphics = new QOpenXRGraphicsD3D12;
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL
    if (graphicsAPI == QSGRendererInterface::OpenGL)
        m_graphics = new QOpenXRGraphicsOpenGL;
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
    if (graphicsAPI == QSGRendererInterface::OpenGL)
        m_graphics = new QOpenXRGraphicsOpenGLES;
#endif

    if (!m_graphics) {
        qDebug() << "The Qt Quick Scenegraph is not using a supported RHI mode:" << graphicsAPI;
        return false;
    }

    // Print out extension and layer information
    checkXrExtensions(nullptr);
    checkXrLayers();

    m_spaceExtension = QOpenXRSpaceExtension::instance();

    // Create Instance
    XrResult result = createXrInstance();
    if (result != XR_SUCCESS) {
        setErrorString(result, "xrCreateInstance");
        delete m_graphics;
        m_graphics = nullptr;
        return false;
    } else {
        checkXrInstance();
    }

    // Catch OpenXR runtime messages via XR_EXT_debug_utils and route them to qDebug
    setupDebugMessenger();

    // Load System
    result = initializeSystem();
    if (result != XR_SUCCESS) {
        setErrorString(result, "xrGetSystem");
        delete m_graphics;
        m_graphics = nullptr;
        return false;
    }

    // Setup Graphics
    if (!setupGraphics()) {
        m_errorString = tr("Failed to set up 3D API integration");
        delete m_graphics;
        m_graphics = nullptr;
        return false;
    }

    // Create Session
    XrSessionCreateInfo xrSessionInfo{};
    xrSessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    xrSessionInfo.next = m_graphics->handle();
    xrSessionInfo.systemId = m_systemId;

    result = xrCreateSession(m_instance, &xrSessionInfo, &m_session);
    if (result != XR_SUCCESS) {
        setErrorString(result, "xrCreateSession");
        delete m_graphics;
        m_graphics = nullptr;
        return false;
    }

    // Meta Quest Specific Setup
    if (m_colorspaceExtensionSupported)
        setupMetaQuestColorSpaces();
    if (m_displayRefreshRateExtensionSupported)
        setupMetaQuestRefreshRates();
    if (m_spaceExtensionSupported)
        m_spaceExtension->initialize(m_instance, m_session);

    checkReferenceSpaces();

    // Setup Input
    m_inputManager = QOpenXRInputManager::instance();
    m_inputManager->init(m_instance, m_session);

    if (!setupAppSpace())
        return false;
    if (!setupViewSpace())
        return false;

    createSwapchains();

    return true;
}

void QOpenXRManager::teardown()
{
    if (m_inputManager) {
        m_inputManager->teardown();
        m_inputManager = nullptr;
    }

    if (m_spaceExtension) {
        m_spaceExtension->teardown();
        m_spaceExtension = nullptr;
    }

    if (m_passthroughLayer)
        destroyMetaQuestPassthroughLayer();
    if (m_passthroughFeature)
        destroyMetaQuestPassthrough();

    destroySwapchain();

    if (m_appSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_appSpace);
    }

    if (m_viewSpace != XR_NULL_HANDLE)
        xrDestroySpace(m_viewSpace);

    xrDestroySession(m_session);

#ifdef XR_EXT_debug_utils
    if (m_debugMessenger) {
        m_xrDestroyDebugUtilsMessengerEXT(m_debugMessenger);
        m_debugMessenger = XR_NULL_HANDLE;
    }
#endif

    xrDestroyInstance(m_instance);
}

void QOpenXRManager::destroySwapchain()
{
    for (Swapchain swapchain : m_swapchains)
        xrDestroySwapchain(swapchain.handle);

    m_swapchains.clear();
    m_swapchainImages.clear();
}

void QOpenXRManager::setPassthroughEnabled(bool enabled)
{
    if (m_enablePassthrough == enabled)
        return;

    m_enablePassthrough = enabled;

    if (m_passthroughSupported) {
        if (m_enablePassthrough) {
            if (m_passthroughFeature == XR_NULL_HANDLE)
                createMetaQuestPassthrough(); // Create and start
            else
                startMetaQuestPassthrough(); // Existed, but not started

            if (m_passthroughLayer == XR_NULL_HANDLE)
                createMetaQuestPassthroughLayer(); // Create
            else
                resumeMetaQuestPassthroughLayer(); // Exist, but not started
        } else {
            // Don't destroy, just pause
            if (m_passthroughLayer)
                pauseMetaQuestPassthroughLayer();

            if (m_passthroughFeature)
                pauseMetaQuestPassthrough();
        }
    }
}

void QOpenXRManager::update()
{
    QEvent *request = new QEvent(QEvent::UpdateRequest);
    QCoreApplication::postEvent(this, request);
}

bool QOpenXRManager::event(QEvent *e)
{
    if (e->type() == QEvent::UpdateRequest) {
        processXrEvents();
        return true;
    }
    return QObject::event(e);
}

void QOpenXRManager::checkXrExtensions(const char *layerName, int indent)
{
    quint32 instanceExtensionCount;
    checkXrResult(xrEnumerateInstanceExtensionProperties(layerName, 0, &instanceExtensionCount, nullptr));

    QVector<XrExtensionProperties> extensions(instanceExtensionCount);
    for (XrExtensionProperties& extension : extensions) {
        extension.type = XR_TYPE_EXTENSION_PROPERTIES;
        extension.next = nullptr;
    }

    checkXrResult(xrEnumerateInstanceExtensionProperties(layerName,
                                                         quint32(extensions.size()),
                                                         &instanceExtensionCount,
                                                         extensions.data()));

    const QByteArray indentStr(indent, ' ');
    qDebug("%sAvailable Extensions: (%d)", indentStr.data(), instanceExtensionCount);
    for (const XrExtensionProperties& extension : extensions) {
        qDebug("%s  Name=%s Version=%d.%d.%d",
               indentStr.data(),
               extension.extensionName,
               XR_VERSION_MAJOR(extension.extensionVersion),
               XR_VERSION_MINOR(extension.extensionVersion),
               XR_VERSION_PATCH(extension.extensionVersion));
    }
}

void QOpenXRManager::checkXrLayers()
{
    quint32 layerCount;
    checkXrResult(xrEnumerateApiLayerProperties(0, &layerCount, nullptr));

    QVector<XrApiLayerProperties> layers(layerCount);
    for (XrApiLayerProperties& layer : layers) {
        layer.type = XR_TYPE_API_LAYER_PROPERTIES;
        layer.next = nullptr;
    }

    checkXrResult(xrEnumerateApiLayerProperties(quint32(layers.size()), &layerCount, layers.data()));

    qDebug("Available Layers: (%d)", layerCount);
    for (const XrApiLayerProperties& layer : layers) {
        qDebug("  Name=%s SpecVersion=%d.%d.%d LayerVersion=%d.%d.%d Description=%s",
               layer.layerName,
               XR_VERSION_MAJOR(layer.specVersion),
               XR_VERSION_MINOR(layer.specVersion),
               XR_VERSION_PATCH(layer.specVersion),
               XR_VERSION_MAJOR(layer.layerVersion),
               XR_VERSION_MINOR(layer.layerVersion),
               XR_VERSION_PATCH(layer.layerVersion),
               layer.description);
        checkXrExtensions(layer.layerName, 4);
    }
}

XrResult QOpenXRManager::createXrInstance()
{
    // Setup Info
    XrApplicationInfo appInfo;
    strcpy(appInfo.applicationName, QCoreApplication::applicationName().toUtf8());
    appInfo.applicationVersion = 7;
    strcpy(appInfo.engineName, QStringLiteral("Qt").toUtf8());
    appInfo.engineVersion = 6;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    // Query available API layers
    uint32_t apiLayerCount = 0;
    xrEnumerateApiLayerProperties(0, &apiLayerCount, nullptr);
    QVector<XrApiLayerProperties> apiLayerProperties(apiLayerCount);
    for (uint32_t i = 0; i < apiLayerCount; i++) {
        apiLayerProperties[i].type = XR_TYPE_API_LAYER_PROPERTIES;
        apiLayerProperties[i].next = nullptr;
    }
    xrEnumerateApiLayerProperties(apiLayerCount, &apiLayerCount, apiLayerProperties.data());

    // Decide which API layers to enable
    QVector<const char*> enabledApiLayers;

    // Now it would be nice if we could use
    // QQuickGraphicsConfiguration::isDebugLayerEnabled() but the quickWindow is
    // nowhere yet, so just replicate the env.var. for now.
    const bool wantsValidationLayer = qEnvironmentVariableIntValue("QSG_RHI_DEBUG_LAYER");
    if (wantsValidationLayer) {
        if (isApiLayerSupported("XR_APILAYER_LUNARG_core_validation", apiLayerProperties))
            enabledApiLayers.append("XR_APILAYER_LUNARG_core_validation");
        else
            qDebug("OpenXR validation layer requested, but not available");
    }

    qDebug() << "Requesting to enable XR API layers:" << enabledApiLayers;

    m_enabledApiLayers.clear();
    for (const char *layer : enabledApiLayers)
        m_enabledApiLayers.append(QString::fromLatin1(layer));

    // Load extensions
    uint32_t extensionCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    QVector<XrExtensionProperties> extensionProperties(extensionCount);
    for (uint32_t i = 0; i < extensionCount; i++) {
        // we usually have to fill in the type (for validation) and set
        // next to NULL (or a pointer to an extension specific struct)
        extensionProperties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
        extensionProperties[i].next = nullptr;
    }
    xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensionProperties.data());

    QVector<const char*> enabledExtensions;
    if (m_graphics->isExtensionSupported(extensionProperties))
        enabledExtensions.append(m_graphics->extensionName());

    if (isExtensionSupported("XR_EXT_debug_utils", extensionProperties))
        enabledExtensions.append("XR_EXT_debug_utils");

    if (isExtensionSupported(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME, extensionProperties))
        enabledExtensions.append(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);

    m_handtrackingExtensionSupported = isExtensionSupported(XR_EXT_HAND_TRACKING_EXTENSION_NAME, extensionProperties);
    if (m_handtrackingExtensionSupported)
        enabledExtensions.append(XR_EXT_HAND_TRACKING_EXTENSION_NAME);

    // Oculus Quest Specific Extensions

    m_handtrackingAimExtensionSupported = isExtensionSupported(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME, extensionProperties);
    if (m_handtrackingAimExtensionSupported)
        enabledExtensions.append(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);

    if (isExtensionSupported(XR_MSFT_HAND_INTERACTION_EXTENSION_NAME, extensionProperties))
        enabledExtensions.append(XR_MSFT_HAND_INTERACTION_EXTENSION_NAME);

    // Passthrough extensions (require manifest feature to work)
    // <uses-feature android:name="com.oculus.feature.PASSTHROUGH" android:required="true" />
    uint32_t passthroughSpecVersion = 0;
    m_passthroughSupported = isExtensionSupported(XR_FB_PASSTHROUGH_EXTENSION_NAME, extensionProperties, &passthroughSpecVersion);
    if (m_passthroughSupported) {
        qDebug("Passthrough extension is supported, spec version %u", passthroughSpecVersion);
        enabledExtensions.append(XR_FB_PASSTHROUGH_EXTENSION_NAME);
    } else {
        qDebug("Passthrough extension is NOT supported");
    }

    if (isExtensionSupported(XR_FB_TRIANGLE_MESH_EXTENSION_NAME, extensionProperties))
        enabledExtensions.append(XR_FB_TRIANGLE_MESH_EXTENSION_NAME);

    m_displayRefreshRateExtensionSupported = isExtensionSupported(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, extensionProperties);
    if (m_displayRefreshRateExtensionSupported)
        enabledExtensions.append(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);

    m_colorspaceExtensionSupported = isExtensionSupported(XR_FB_COLOR_SPACE_EXTENSION_NAME, extensionProperties);
    if (m_colorspaceExtensionSupported)
        enabledExtensions.append(XR_FB_COLOR_SPACE_EXTENSION_NAME);

    if (isExtensionSupported(XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME, extensionProperties))
        enabledExtensions.append(XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME);

    m_foveationExtensionSupported = isExtensionSupported(XR_FB_FOVEATION_EXTENSION_NAME, extensionProperties);
    if (m_foveationExtensionSupported)
        enabledExtensions.append(XR_FB_FOVEATION_EXTENSION_NAME);

    if (isExtensionSupported(XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME, extensionProperties))
        enabledExtensions.append(XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME);

    if (m_spaceExtension) {
        const auto requiredExtensions = m_spaceExtension->requiredExtensions();
        bool isSupported = true;
        for (const auto extension : requiredExtensions) {
            isSupported = isExtensionSupported(extension, extensionProperties) && isSupported;
            if (!isSupported)
                break;
        }
        m_spaceExtensionSupported = isSupported;
        if (isSupported)
            enabledExtensions.append(requiredExtensions);
    }

#ifdef Q_OS_ANDROID
    if (isExtensionSupported(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME, extensionProperties))
        enabledExtensions.append(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME);

    auto graphicsAPI = QQuickWindow::graphicsApi();
    if (graphicsAPI == QSGRendererInterface::Vulkan) {
        if (isExtensionSupported(XR_FB_SWAPCHAIN_UPDATE_STATE_VULKAN_EXTENSION_NAME, extensionProperties))
            enabledExtensions.append(XR_FB_SWAPCHAIN_UPDATE_STATE_VULKAN_EXTENSION_NAME);
    } else if (graphicsAPI == QSGRendererInterface::OpenGL) {
        if (isExtensionSupported(XR_FB_SWAPCHAIN_UPDATE_STATE_OPENGL_ES_EXTENSION_NAME, extensionProperties))
            enabledExtensions.append(XR_FB_SWAPCHAIN_UPDATE_STATE_OPENGL_ES_EXTENSION_NAME);
    }
#endif

    qDebug() << "Requesting to enable XR extensions:" << enabledExtensions;

    m_enabledExtensions.clear();
    for (const char *extension : enabledExtensions)
        m_enabledExtensions.append(QString::fromLatin1(extension));

    // Create Instance
    XrInstanceCreateInfo xrInstanceInfo{};
    xrInstanceInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    xrInstanceInfo.next = nullptr;
    xrInstanceInfo.createFlags = 0;
    xrInstanceInfo.applicationInfo = appInfo;
    xrInstanceInfo.enabledApiLayerCount = enabledApiLayers.count();
    xrInstanceInfo.enabledApiLayerNames = enabledApiLayers.constData();
    xrInstanceInfo.enabledExtensionCount = enabledExtensions.count();
    xrInstanceInfo.enabledExtensionNames = enabledExtensions.constData();

    return xrCreateInstance(&xrInstanceInfo, &m_instance);
}

void QOpenXRManager::checkXrInstance()
{
    Q_ASSERT(m_instance != XR_NULL_HANDLE);
    XrInstanceProperties instanceProperties{};
    instanceProperties.type = XR_TYPE_INSTANCE_PROPERTIES;
    checkXrResult(xrGetInstanceProperties(m_instance, &instanceProperties));

    m_runtimeName = QString::fromUtf8(instanceProperties.runtimeName);
    m_runtimeVersion = QVersionNumber(XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
                                      XR_VERSION_MINOR(instanceProperties.runtimeVersion),
                                      XR_VERSION_PATCH(instanceProperties.runtimeVersion));

    qDebug("Instance RuntimeName=%s RuntimeVersion=%d.%d.%d",
           qPrintable(m_runtimeName),
           m_runtimeVersion.majorVersion(),
           m_runtimeVersion.minorVersion(),
           m_runtimeVersion.microVersion());
}

void QOpenXRManager::setupDebugMessenger()
{
    if (!m_enabledExtensions.contains(QString::fromUtf8("XR_EXT_debug_utils"))) {
        qDebug("Quick 3D XR: No debug utils extension, message redirection not set up");
        return;
    }

#ifdef XR_EXT_debug_utils
    PFN_xrCreateDebugUtilsMessengerEXT xrCreateDebugUtilsMessengerEXT = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrCreateDebugUtilsMessengerEXT",
                                        reinterpret_cast<PFN_xrVoidFunction *>(&xrCreateDebugUtilsMessengerEXT)));
    if (!xrCreateDebugUtilsMessengerEXT)
        return;

    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrDestroyDebugUtilsMessengerEXT",
                                        reinterpret_cast<PFN_xrVoidFunction *>(&m_xrDestroyDebugUtilsMessengerEXT)));

    XrDebugUtilsMessengerCreateInfoEXT messengerInfo = {};
    messengerInfo.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerInfo.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
            | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
    messengerInfo.userCallback = defaultDebugCallbackFunc;
    messengerInfo.userData = this;

    XrResult err = xrCreateDebugUtilsMessengerEXT(m_instance, &messengerInfo, &m_debugMessenger);
    if (!checkXrResult(err))
        qWarning("Quick 3D XR: Failed to create debug report callback, OpenXR messages will not get redirected (%d)", err);
#endif
}

XrResult QOpenXRManager::initializeSystem()
{
    Q_ASSERT(m_instance != XR_NULL_HANDLE);
    Q_ASSERT(m_systemId == XR_NULL_SYSTEM_ID);

    XrSystemGetInfo hmdInfo{};
    hmdInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    hmdInfo.next = nullptr;
    hmdInfo.formFactor = m_formFactor;

    const XrResult result = xrGetSystem(m_instance, &hmdInfo, &m_systemId);
    const bool success = checkXrResult(result);

    if (!success)
        return result;

    // Check View Configuration
    checkViewConfiguration();

    return result;
}

void QOpenXRManager::checkViewConfiguration()
{
    quint32 viewConfigTypeCount;
    checkXrResult(xrEnumerateViewConfigurations(m_instance,
                                                m_systemId,
                                                0,
                                                &viewConfigTypeCount,
                                                nullptr));
    QVector<XrViewConfigurationType> viewConfigTypes(viewConfigTypeCount);
    checkXrResult(xrEnumerateViewConfigurations(m_instance,
                                                m_systemId,
                                                viewConfigTypeCount,
                                                &viewConfigTypeCount,
                                                viewConfigTypes.data()));

    qDebug("Available View Configuration Types: (%d)", viewConfigTypeCount);
    for (XrViewConfigurationType viewConfigType : viewConfigTypes) {
        qDebug("  View Configuration Type: %s %s", to_string(viewConfigType), viewConfigType == m_viewConfigType ? "(Selected)" : "");
        XrViewConfigurationProperties viewConfigProperties{};
        viewConfigProperties.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
        checkXrResult(xrGetViewConfigurationProperties(m_instance,
                                                       m_systemId,
                                                       viewConfigType,
                                                       &viewConfigProperties));

        qDebug("  View configuration FovMutable=%s", viewConfigProperties.fovMutable == XR_TRUE ? "True" : "False");

        uint32_t viewCount;
        checkXrResult(xrEnumerateViewConfigurationViews(m_instance,
                                                        m_systemId,
                                                        viewConfigType,
                                                        0,
                                                        &viewCount,
                                                        nullptr));
        if (viewCount > 0) {
            QVector<XrViewConfigurationView> views(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 0, 0, 0, 0, 0, 0});
            checkXrResult(xrEnumerateViewConfigurationViews(m_instance,
                                                            m_systemId,
                                                            viewConfigType,
                                                            viewCount,
                                                            &viewCount,
                                                            views.data()));
            for (int i = 0; i < views.size(); ++i) {
                const XrViewConfigurationView& view = views[i];
                qDebug("    View [%d]: Recommended Width=%d Height=%d SampleCount=%d",
                       i,
                       view.recommendedImageRectWidth,
                       view.recommendedImageRectHeight,
                       view.recommendedSwapchainSampleCount);
                qDebug("    View [%d]:     Maximum Width=%d Height=%d SampleCount=%d",
                       i,
                       view.maxImageRectWidth,
                       view.maxImageRectHeight,
                       view.maxSwapchainSampleCount);
            }
        } else {
            qDebug("Empty view configuration type");
        }
        checkEnvironmentBlendMode(viewConfigType);
    }
}
bool QOpenXRManager::checkXrResult(const XrResult &result)
{
    return OpenXRHelpers::checkXrResult(result, m_instance);
}

void QOpenXRManager::checkEnvironmentBlendMode(XrViewConfigurationType type)
{
    uint32_t count;
    checkXrResult(xrEnumerateEnvironmentBlendModes(m_instance,
                                                   m_systemId,
                                                   type,
                                                   0,
                                                   &count,
                                                   nullptr));

    qDebug("Available Environment Blend Mode count : (%d)", count);

    QVector<XrEnvironmentBlendMode> blendModes(count);
    checkXrResult(xrEnumerateEnvironmentBlendModes(m_instance,
                                                   m_systemId,
                                                   type,
                                                   count,
                                                   &count,
                                                   blendModes.data()));

    bool blendModeFound = false;
    for (XrEnvironmentBlendMode mode : blendModes) {
        const bool blendModeMatch = (mode == m_environmentBlendMode);
        qDebug("Environment Blend Mode (%s) : %s", to_string(mode), blendModeMatch ? "(Selected)" : "");
        blendModeFound |= blendModeMatch;
    }
    if (!blendModeFound)
        qWarning("No matching environment blend mode found");
}

bool QOpenXRManager::setupGraphics()
{
    preSetupQuickScene();

    if (!m_graphics->setupGraphics(m_instance, m_systemId, m_quickWindow->graphicsConfiguration()))
        return false;

    if (!setupQuickScene())
        return false;

    QRhi *rhi = m_quickWindow->rhi();

#if QT_CONFIG(graphicsframecapture)
    if (m_frameCapture) {
        m_frameCapture->setCapturePath(QLatin1String("."));
        m_frameCapture->setCapturePrefix(QLatin1String("quick3dxr"));
        m_frameCapture->setRhi(rhi);
        if (!m_frameCapture->isLoaded()) {
            qWarning("Quick 3D XR: Frame capture was requested but QGraphicsFrameCapture is not initialized"
                     " (or has no backends enabled in the Qt build)");
        } else {
            qDebug("Quick 3D XR: Frame capture initialized");
        }
    }
#endif

    return m_graphics->finializeGraphics(rhi);
}

void QOpenXRManager::checkReferenceSpaces()
{
    Q_ASSERT(m_session != XR_NULL_HANDLE);

    uint32_t spaceCount;
    checkXrResult(xrEnumerateReferenceSpaces(m_session, 0, &spaceCount, nullptr));
    m_availableReferenceSpace.resize(spaceCount);
    checkXrResult(xrEnumerateReferenceSpaces(m_session, spaceCount, &spaceCount, m_availableReferenceSpace.data()));

    qDebug("Available reference spaces: %d", spaceCount);
    for (XrReferenceSpaceType space : m_availableReferenceSpace) {
        qDebug("  Name: %s", to_string(space));
    }
}

bool QOpenXRManager::isReferenceSpaceAvailable(XrReferenceSpaceType type)
{
    return m_availableReferenceSpace.contains(type);
}

bool QOpenXRManager::setupAppSpace()
{
    Q_ASSERT(m_session != XR_NULL_HANDLE);

    XrPosef identityPose;
    identityPose.orientation.w = 1;
    identityPose.orientation.x = 0;
    identityPose.orientation.y = 0;
    identityPose.orientation.z = 0;
    identityPose.position.x = 0;
    identityPose.position.y = 0;
    identityPose.position.z = 0;

    XrReferenceSpaceType newReferenceSpace;
    XrSpace newAppSpace = XR_NULL_HANDLE;
    m_isEmulatingLocalFloor = false;

    if (isReferenceSpaceAvailable(m_requestedReferenceSpace)) {
        newReferenceSpace = m_requestedReferenceSpace;
    } else if (m_requestedReferenceSpace == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT &&
               isReferenceSpaceAvailable(XR_REFERENCE_SPACE_TYPE_STAGE)) {
        m_isEmulatingLocalFloor = true;
        m_isFloorResetPending = true;
        newReferenceSpace = XR_REFERENCE_SPACE_TYPE_LOCAL;
    } else {
        qWarning("Requested reference space is not available");
        newReferenceSpace = XR_REFERENCE_SPACE_TYPE_LOCAL;
    }

    // App Space
    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{};
    referenceSpaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    referenceSpaceCreateInfo.poseInReferenceSpace = identityPose;
    referenceSpaceCreateInfo.referenceSpaceType = newReferenceSpace;
    if (!checkXrResult(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &newAppSpace))) {
        qWarning("Failed to create app space");
        return false;
    }

    if (m_appSpace)
        xrDestroySpace(m_appSpace);

    m_appSpace = newAppSpace;
    m_referenceSpace = newReferenceSpace;
    // only broadcast the reference space change if we are not emulating the local floor
    // since we'll try and change the referenceSpace again once we have tracking
    if (!m_isFloorResetPending)
        emit referenceSpaceChanged();

    return true;

}

void QOpenXRManager::updateAppSpace(XrTime predictedDisplayTime)
{
    // If the requested reference space is not the current one, we need to
    // re-create the app space now
    if (m_requestedReferenceSpace != m_referenceSpace && !m_isFloorResetPending) {
        if (!setupAppSpace()) {
            // If we can't set the requested reference space, use the current one
            qWarning("Setting requested reference space failed");
            m_requestedReferenceSpace = m_referenceSpace;
            return;
        }
    }

    // This happens when we setup the emulated LOCAL_FLOOR mode
    // We may have requested it on app setup, but we need to have
    // some tracking information to calculate the floor height so
    // that will only happen once we get here.
    if (m_isFloorResetPending) {
        if (!resetEmulatedFloorHeight(predictedDisplayTime)) {
            // It didn't work, so give up and use local space (which is already setup).
            m_requestedReferenceSpace = XR_REFERENCE_SPACE_TYPE_LOCAL;
            emit referenceSpaceChanged();
        }
        return;
    }

}

bool QOpenXRManager::setupViewSpace()
{
    Q_ASSERT(m_session != XR_NULL_HANDLE);

    XrPosef identityPose;
    identityPose.orientation.w = 1;
    identityPose.orientation.x = 0;
    identityPose.orientation.y = 0;
    identityPose.orientation.z = 0;
    identityPose.position.x = 0;
    identityPose.position.y = 0;
    identityPose.position.z = 0;

    XrSpace newViewSpace = XR_NULL_HANDLE;

    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{};
    referenceSpaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    referenceSpaceCreateInfo.poseInReferenceSpace = identityPose;
    referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!checkXrResult(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &newViewSpace))) {
        qWarning("Failed to create view space");
        return false;
    }

    if (m_viewSpace != XR_NULL_HANDLE)
        xrDestroySpace(m_viewSpace);

    m_viewSpace = newViewSpace;

    return true;
}

bool QOpenXRManager::resetEmulatedFloorHeight(XrTime predictedDisplayTime)
{
    Q_ASSERT(m_isEmulatingLocalFloor);

    m_isFloorResetPending = false;

    XrPosef identityPose;
    identityPose.orientation.w = 1;
    identityPose.orientation.x = 0;
    identityPose.orientation.y = 0;
    identityPose.orientation.z = 0;
    identityPose.position.x = 0;
    identityPose.position.y = 0;
    identityPose.position.z = 0;

    XrSpace localSpace = XR_NULL_HANDLE;
    XrSpace stageSpace = XR_NULL_HANDLE;

    XrReferenceSpaceCreateInfo createInfo{};
    createInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    createInfo.poseInReferenceSpace = identityPose;

    if (!checkXrResult(xrCreateReferenceSpace(m_session, &createInfo, &localSpace))) {
        qWarning("Failed to create local space (for emulated LOCAL_FLOOR space)");
        return false;
    }

    createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (!checkXrResult(xrCreateReferenceSpace(m_session, &createInfo, &stageSpace))) {
        qWarning("Failed to create stage space (for emulated LOCAL_FLOOR space)");
        xrDestroySpace(localSpace);
        return false;
    }

    XrSpaceLocation stageLocation{};
    stageLocation.type = XR_TYPE_SPACE_LOCATION;
    stageLocation.pose = identityPose;

    if (!checkXrResult(xrLocateSpace(stageSpace, localSpace, predictedDisplayTime, &stageLocation))) {
        qWarning("Failed to locate STAGE space in LOCAL space, in order to emulate LOCAL_FLOOR");
        xrDestroySpace(localSpace);
        xrDestroySpace(stageSpace);
        return false;
    }

    xrDestroySpace(localSpace);
    xrDestroySpace(stageSpace);

    XrSpace newAppSpace = XR_NULL_HANDLE;
    createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    createInfo.poseInReferenceSpace.position.y = stageLocation.pose.position.y;
    if (!checkXrResult(xrCreateReferenceSpace(m_session, &createInfo, &newAppSpace))) {
        qWarning("Failed to recreate emulated LOCAL_FLOOR play space with latest floor estimate");
        return false;
    }

    xrDestroySpace(m_appSpace);
    m_appSpace = newAppSpace;
    m_referenceSpace = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
    emit referenceSpaceChanged();

    return true;
}

void QOpenXRManager::createSwapchains()
{
    Q_ASSERT(m_session != XR_NULL_HANDLE);
    Q_ASSERT(m_configViews.isEmpty());
    Q_ASSERT(m_swapchains.isEmpty());

    XrSystemProperties systemProperties{};
    systemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;

    XrSystemHandTrackingPropertiesEXT handTrackingSystemProperties{};
    handTrackingSystemProperties.type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT;
    systemProperties.next = &handTrackingSystemProperties;

    checkXrResult(xrGetSystemProperties(m_instance, m_systemId, &systemProperties));
    qDebug("System Properties: Name=%s VendorId=%d", systemProperties.systemName, systemProperties.vendorId);
    qDebug("System Graphics Properties: MaxWidth=%d MaxHeight=%d MaxLayers=%d",
           systemProperties.graphicsProperties.maxSwapchainImageWidth,
           systemProperties.graphicsProperties.maxSwapchainImageHeight,
           systemProperties.graphicsProperties.maxLayerCount);
    qDebug("System Tracking Properties: OrientationTracking=%s PositionTracking=%s",
           systemProperties.trackingProperties.orientationTracking == XR_TRUE ? "True" : "False",
           systemProperties.trackingProperties.positionTracking == XR_TRUE ? "True" : "False");
    qDebug("System Hand Tracking Properties: handTracking=%s",
           handTrackingSystemProperties.supportsHandTracking == XR_TRUE ? "True" : "False");

    // View Config type has to be Stereo, because OpenXR doesn't support any other mode yet.
    quint32 viewCount;
    checkXrResult(xrEnumerateViewConfigurationViews(m_instance,
                                                    m_systemId,
                                                    m_viewConfigType,
                                                    0,
                                                    &viewCount,
                                                    nullptr));
    m_configViews.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 0, 0, 0, 0, 0, 0});
    checkXrResult(xrEnumerateViewConfigurationViews(m_instance,
                                                    m_systemId,
                                                    m_viewConfigType,
                                                    viewCount,
                                                    &viewCount,
                                                    m_configViews.data()));
    m_views.resize(viewCount, {XR_TYPE_VIEW, nullptr, {}, {}});
    m_projectionLayerViews.resize(viewCount, {});

    // Create the swapchain and get the images.
    if (viewCount > 0) {
        // Select a swapchain format.
        uint32_t swapchainFormatCount;
        checkXrResult(xrEnumerateSwapchainFormats(m_session, 0, &swapchainFormatCount, nullptr));
        QVector<int64_t> swapchainFormats(swapchainFormatCount);
        checkXrResult(xrEnumerateSwapchainFormats(m_session,
                                                  swapchainFormats.size(),
                                                  &swapchainFormatCount,
                                                  swapchainFormats.data()));
        Q_ASSERT(swapchainFormatCount == swapchainFormats.size());
        m_colorSwapchainFormat = m_graphics->colorSwapchainFormat(swapchainFormats);

        // Print swapchain formats and the selected one.
        {
            QString swapchainFormatsString;
            for (int64_t format : swapchainFormats) {
                const bool selected = format == m_colorSwapchainFormat;
                swapchainFormatsString += u" ";
                if (selected) {
                    swapchainFormatsString += u"[";
                }
                swapchainFormatsString += QString::number(format);
                if (selected) {
                    swapchainFormatsString += u"]";
                }
            }
            qDebug("Swapchain Formats: %s", qPrintable(swapchainFormatsString));
        }

        const XrViewConfigurationView &vp = m_configViews[0]; // use the first view for all views, the sizes should be the same

        // sampleCount for the XrSwapchain is always 1. We could take m_samples
        // here, clamp it to vp.maxSwapchainSampleCount, and pass it in to the
        // swapchain to get multisample textures (or a multisample texture
        // array) out of the swapchain. This we do not do, because it was only
        // supported with 1 out of 5 OpenXR(+streaming) combination tested on
        // the Quest 3. In most cases, incl. Quest 3 native Android,
        // maxSwapchainSampleCount is 1. Therefore, we do MSAA on our own, and
        // do not rely on the XrSwapchain for this.

        if (m_multiviewRendering) {
            // Create a single swapchain with array size > 1
            XrSwapchainCreateInfo swapchainCreateInfo{};
            swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
            swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
            swapchainCreateInfo.format = m_colorSwapchainFormat;
            swapchainCreateInfo.sampleCount = 1; // we do MSAA on our own, do not need ms textures from the swapchain
            swapchainCreateInfo.width = vp.recommendedImageRectWidth;
            swapchainCreateInfo.height = vp.recommendedImageRectHeight;
            swapchainCreateInfo.faceCount = 1;
            swapchainCreateInfo.arraySize = viewCount;
            swapchainCreateInfo.mipCount = 1;

            qDebug("Creating multiview swapchain for %d view(s) with dimensions Width=%d Height=%d SampleCount=%d Format=%llx",
                viewCount,
                vp.recommendedImageRectWidth,
                vp.recommendedImageRectHeight,
                1,
                static_cast<long long unsigned int>(m_colorSwapchainFormat));

            Swapchain swapchain;
            swapchain.width = swapchainCreateInfo.width;
            swapchain.height = swapchainCreateInfo.height;
            swapchain.arraySize = swapchainCreateInfo.arraySize;
            if (!checkXrResult(xrCreateSwapchain(m_session, &swapchainCreateInfo, &swapchain.handle)))
                qWarning("xrCreateSwapchain failed");

            m_swapchains.append(swapchain);

            uint32_t imageCount;
            checkXrResult(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr));

            auto swapchainImages = m_graphics->allocateSwapchainImages(imageCount, swapchain.handle);
            checkXrResult(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount, swapchainImages[0]));

            m_swapchainImages.insert(swapchain.handle, swapchainImages);
        } else {
            // Create a swapchain for each view.
            for (uint32_t i = 0; i < viewCount; i++) {
                qDebug("Creating swapchain for view %d with dimensions Width=%d Height=%d SampleCount=%d Format=%llx",
                    i,
                    vp.recommendedImageRectWidth,
                    vp.recommendedImageRectHeight,
                    1,
                    static_cast<long long unsigned int>(m_colorSwapchainFormat));

                // Create the swapchain.
                XrSwapchainCreateInfo swapchainCreateInfo{};
                swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
                swapchainCreateInfo.arraySize = 1;
                swapchainCreateInfo.format = m_colorSwapchainFormat;
                swapchainCreateInfo.width = vp.recommendedImageRectWidth;
                swapchainCreateInfo.height = vp.recommendedImageRectHeight;
                swapchainCreateInfo.mipCount = 1;
                swapchainCreateInfo.faceCount = 1;
                swapchainCreateInfo.sampleCount = 1; // we do MSAA on our own, do not need ms textures from the swapchain
                swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
                Swapchain swapchain;
                swapchain.width = swapchainCreateInfo.width;
                swapchain.height = swapchainCreateInfo.height;
                if (!checkXrResult(xrCreateSwapchain(m_session, &swapchainCreateInfo, &swapchain.handle)))
                    qWarning("xrCreateSwapchain failed");

                m_swapchains.append(swapchain);

                uint32_t imageCount;
                checkXrResult(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr));

                auto swapchainImages = m_graphics->allocateSwapchainImages(imageCount, swapchain.handle);
                checkXrResult(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount, swapchainImages[0]));

                m_swapchainImages.insert(swapchain.handle, swapchainImages);
            }
        }

        // Setup the projection layer views.
        for (uint32_t i = 0; i < viewCount; ++i) {
            m_projectionLayerViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            m_projectionLayerViews[i].next = nullptr;
            m_projectionLayerViews[i].subImage.swapchain = m_swapchains[0].handle; // for non-multiview this gets overwritten later
            m_projectionLayerViews[i].subImage.imageArrayIndex = i; // this too
            m_projectionLayerViews[i].subImage.imageRect.offset.x = 0;
            m_projectionLayerViews[i].subImage.imageRect.offset.y = 0;
            m_projectionLayerViews[i].subImage.imageRect.extent.width = vp.recommendedImageRectWidth;
            m_projectionLayerViews[i].subImage.imageRect.extent.height = vp.recommendedImageRectHeight;
        }
    }

    if (m_foveationExtensionSupported)
        setupMetaQuestFoveation();
}

void QOpenXRManager::setSamples(int samples)
{
    if (m_samples == samples)
        return;

    m_samples = samples;

    // No need to do anything more here (such as destroying and recreating the
    // XrSwapchain) since we do not do MSAA through the swapchain.
}

void QOpenXRManager::processXrEvents()
{
    bool exitRenderLoop = false;
    bool requestrestart = false;
    pollEvents(&exitRenderLoop, &requestrestart);

    if (exitRenderLoop)
        emit sessionEnded();

    if (m_sessionRunning) {
        m_inputManager->pollActions();
        renderFrame();
    }
    update();
}

void QOpenXRManager::pollEvents(bool *exitRenderLoop, bool *requestRestart) {
    *exitRenderLoop = false;
    *requestRestart = false;

    auto readNextEvent = [this]() {
        // It is sufficient to clear the just the XrEventDataBuffer header to
        // XR_TYPE_EVENT_DATA_BUFFER
        XrEventDataBaseHeader* baseHeader = reinterpret_cast<XrEventDataBaseHeader*>(&m_eventDataBuffer);
        *baseHeader = {XR_TYPE_EVENT_DATA_BUFFER, nullptr};
        const XrResult xr = xrPollEvent(m_instance, &m_eventDataBuffer);
        if (xr == XR_SUCCESS) {
            if (baseHeader->type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
                const XrEventDataEventsLost* const eventsLost = reinterpret_cast<const XrEventDataEventsLost*>(baseHeader);
                qDebug("%d events lost", eventsLost->lostEventCount);
            }

            return baseHeader;
        }

        return static_cast<XrEventDataBaseHeader*>(nullptr);
    };

    auto handleSessionStateChangedEvent = [this](const XrEventDataSessionStateChanged& stateChangedEvent,
            bool* exitRenderLoop,
            bool* requestRestart) {
        const XrSessionState oldState = m_sessionState;
        m_sessionState = stateChangedEvent.state;

        qDebug("XrEventDataSessionStateChanged: state %s->%s time=%lld",
               to_string(oldState),
               to_string(m_sessionState),
               static_cast<long long int>(stateChangedEvent.time));

        if ((stateChangedEvent.session != XR_NULL_HANDLE) && (stateChangedEvent.session != m_session)) {
            qDebug("XrEventDataSessionStateChanged for unknown session");
            return;
        }

        switch (m_sessionState) {
        case XR_SESSION_STATE_READY: {
            Q_ASSERT(m_session != XR_NULL_HANDLE);
            XrSessionBeginInfo sessionBeginInfo{};
            sessionBeginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
            sessionBeginInfo.primaryViewConfigurationType = m_viewConfigType;
            checkXrResult(xrBeginSession(m_session, &sessionBeginInfo));
            m_sessionRunning = true;
            break;
        }
        case XR_SESSION_STATE_STOPPING: {
            Q_ASSERT(m_session != XR_NULL_HANDLE);
            m_sessionRunning = false;
            checkXrResult(xrEndSession(m_session));
            break;
        }
        case XR_SESSION_STATE_EXITING: {
            *exitRenderLoop = true;
            // Do not attempt to restart because user closed this session.
            *requestRestart = false;
            break;
        }
        case XR_SESSION_STATE_LOSS_PENDING: {
            *exitRenderLoop = true;
            // Poll for a new instance.
            *requestRestart = true;
            break;
        }
        default:
            break;
        }
    };

    // Process all pending messages.
    while (const XrEventDataBaseHeader* event = readNextEvent()) {
        switch (event->type) {
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
            const auto& instanceLossPending = *reinterpret_cast<const XrEventDataInstanceLossPending*>(event);
            qDebug("XrEventDataInstanceLossPending by %lld", static_cast<long long int>(instanceLossPending.lossTime));
            *exitRenderLoop = true;
            *requestRestart = true;
            return;
        }
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto sessionStateChangedEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(event);
            handleSessionStateChangedEvent(sessionStateChangedEvent, exitRenderLoop, requestRestart);
            break;
        }
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            break;
        case XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB:
        case XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB:
        case XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB:
        case XR_TYPE_EVENT_DATA_SCENE_CAPTURE_COMPLETE_FB:
            // Handle these events in the space extension
            if (m_spaceExtension)
                m_spaceExtension->handleEvent(event);
            break;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
        default: {
            qDebug("Ignoring event type %d", event->type);
            break;
        }
        }
    }
}

void QOpenXRManager::renderFrame()
{
    Q_ASSERT(m_session != XR_NULL_HANDLE);

    XrFrameWaitInfo frameWaitInfo{};
    frameWaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
    XrFrameState frameState{};
    frameState.type = XR_TYPE_FRAME_STATE;
    checkXrResult(xrWaitFrame(m_session, &frameWaitInfo, &frameState));

    XrFrameBeginInfo frameBeginInfo{};
    frameBeginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
    checkXrResult(xrBeginFrame(m_session, &frameBeginInfo));

    QVector<XrCompositionLayerBaseHeader*> layers;

    XrCompositionLayerPassthroughFB passthroughCompLayer{};
    passthroughCompLayer.type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB;
    if (m_enablePassthrough && m_passthroughSupported) {
        if (m_passthroughLayer == XR_NULL_HANDLE)
            createMetaQuestPassthroughLayer();
        passthroughCompLayer.layerHandle = m_passthroughLayer;
        passthroughCompLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        passthroughCompLayer.space = XR_NULL_HANDLE;
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&passthroughCompLayer));
    }

    XrCompositionLayerProjection layer{};
    layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    layer.layerFlags |= XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
    layer.layerFlags |= XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;

    if (frameState.shouldRender == XR_TRUE) {
        if (renderLayer(frameState.predictedDisplayTime, frameState.predictedDisplayPeriod, layer)) {
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
        }
    }

    XrFrameEndInfo frameEndInfo{};
    frameEndInfo.type = XR_TYPE_FRAME_END_INFO;
    frameEndInfo.displayTime = frameState.predictedDisplayTime;
    if (!m_enablePassthrough)
        frameEndInfo.environmentBlendMode = m_environmentBlendMode;
    else
        frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    frameEndInfo.layerCount = (uint32_t)layers.size();
    frameEndInfo.layers = layers.data();
    checkXrResult(xrEndFrame(m_session, &frameEndInfo));
}

bool QOpenXRManager::renderLayer(XrTime predictedDisplayTime,
                                XrDuration predictedDisplayPeriod,
                                XrCompositionLayerProjection &layer)
{
    XrResult res;

    XrViewState viewState{};
    viewState.type = XR_TYPE_VIEW_STATE;
    quint32 viewCapacityInput = m_views.size();
    quint32 viewCountOutput;

    // Check if we need to update the app space before we use it
    updateAppSpace(predictedDisplayTime);

    XrViewLocateInfo viewLocateInfo{};
    viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
    viewLocateInfo.viewConfigurationType = m_viewConfigType;
    viewLocateInfo.displayTime = predictedDisplayTime;
    viewLocateInfo.space = m_appSpace;

    res = xrLocateViews(m_session, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, m_views.data());
    checkXrResult(res);
    if (XR_UNQUALIFIED_SUCCESS(res)) {
        Q_ASSERT(viewCountOutput == viewCapacityInput);
        Q_ASSERT(viewCountOutput == m_configViews.size());
        Q_ASSERT(viewCountOutput == m_projectionLayerViews.size());
        Q_ASSERT(m_multiviewRendering ? viewCountOutput == m_swapchains[0].arraySize : viewCountOutput == m_swapchains.size());

        // Check for XrOrigin
        checkOrigin();

        // Update the camera/head position
        XrSpaceLocation location{};
        location.type = XR_TYPE_SPACE_LOCATION;
        if (checkXrResult(xrLocateSpace(m_viewSpace, m_appSpace, predictedDisplayTime, &location))) {
            m_xrOrigin->camera()->setPosition(QVector3D(location.pose.position.x,
                                                        location.pose.position.y,
                                                        location.pose.position.z) * 100.0f); // convert m to cm
            m_xrOrigin->camera()->setRotation(QQuaternion(location.pose.orientation.w,
                                                          location.pose.orientation.x,
                                                          location.pose.orientation.y,
                                                          location.pose.orientation.z));
        }

        // Set the hand positions
        m_inputManager->updatePoses(predictedDisplayTime, m_appSpace);

        // Spatial Anchors
        if (m_spaceExtension)
            m_spaceExtension->updateAnchors(predictedDisplayTime, m_appSpace);

        if (m_handtrackingExtensionSupported)
            m_inputManager->updateHandtracking(predictedDisplayTime, m_appSpace, m_handtrackingAimExtensionSupported);

        // Before rendering individual views, advance the animation driver once according
        // to the expected display time

        const qint64 displayPeriodMS = predictedDisplayPeriod / 1000000;
        const qint64 displayDeltaMS = (predictedDisplayTime - m_previousTime) / 1000000;

        if (m_previousTime == 0)
            m_animationDriver->setStep(displayPeriodMS);
        else {
            if (displayDeltaMS > displayPeriodMS)
                m_animationDriver->setStep(displayPeriodMS);
            else
                m_animationDriver->setStep(displayDeltaMS);
            m_animationDriver->advance();
        }
        m_previousTime = predictedDisplayTime;

#if QT_CONFIG(graphicsframecapture)
        if (m_frameCapture)
            m_frameCapture->startCaptureFrame();
#endif

        if (m_multiviewRendering) {
            const Swapchain swapchain = m_swapchains[0];

            // Acquire the swapchain image array
            XrSwapchainImageAcquireInfo acquireInfo{};
            acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

            uint32_t swapchainImageIndex;
            checkXrResult(xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &swapchainImageIndex));

            XrSwapchainImageWaitInfo waitInfo{};
            waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
            waitInfo.timeout = XR_INFINITE_DURATION;
            checkXrResult(xrWaitSwapchainImage(swapchain.handle, &waitInfo));

            const XrSwapchainImageBaseHeader* const swapchainImage =
                m_swapchainImages[swapchain.handle][swapchainImageIndex];

            // First update both cameras with the latest view information and
            // then set them on the viewport (since this is going to be
            // multiview rendering).
            for (uint32_t i = 0; i < viewCountOutput; i++) {
                // subImage.swapchain and imageArrayIndex are already set and correct
                m_projectionLayerViews[i].pose = m_views[i].pose;
                m_projectionLayerViews[i].fov = m_views[i].fov;
            }
            updateCameraMultiview(0, viewCountOutput);

            // Perform the rendering. In multiview mode it is done just once,
            // targeting all the views (outputting simultaneously to all texture
            // array layers). The subImage dimensions are the same, that's why
            // passing in the first layerView's subImage works.
            doRender(m_projectionLayerViews[0].subImage, swapchainImage);

            // release the swapchain image array
            XrSwapchainImageReleaseInfo releaseInfo{};
            releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            checkXrResult(xrReleaseSwapchainImage(swapchain.handle, &releaseInfo));
        } else {
            for (uint32_t i = 0; i < viewCountOutput; i++) {
                // Each view has a separate swapchain which is acquired, rendered to, and released.
                const Swapchain viewSwapchain = m_swapchains[i];

                // Render view to the appropriate part of the swapchain image.
                XrSwapchainImageAcquireInfo acquireInfo{};
                acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

                uint32_t swapchainImageIndex;
                checkXrResult(xrAcquireSwapchainImage(viewSwapchain.handle, &acquireInfo, &swapchainImageIndex));

                XrSwapchainImageWaitInfo waitInfo{};
                waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
                waitInfo.timeout = XR_INFINITE_DURATION;
                checkXrResult(xrWaitSwapchainImage(viewSwapchain.handle, &waitInfo));

                const XrSwapchainImageBaseHeader* const swapchainImage =
                        m_swapchainImages[viewSwapchain.handle][swapchainImageIndex];

                m_projectionLayerViews[i].subImage.swapchain = viewSwapchain.handle;
                m_projectionLayerViews[i].subImage.imageArrayIndex = 0;
                m_projectionLayerViews[i].pose = m_views[i].pose;
                m_projectionLayerViews[i].fov = m_views[i].fov;

                updateCameraNonMultiview(i, m_projectionLayerViews[i]);

                doRender(m_projectionLayerViews[i].subImage, swapchainImage);

                XrSwapchainImageReleaseInfo releaseInfo{};
                releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                checkXrResult(xrReleaseSwapchainImage(viewSwapchain.handle, &releaseInfo));
            }
        }

#if QT_CONFIG(graphicsframecapture)
        if (m_frameCapture)
            m_frameCapture->endCaptureFrame();
#endif

        layer.space = m_appSpace;
        layer.viewCount = (uint32_t)m_projectionLayerViews.size();
        layer.views = m_projectionLayerViews.data();
        return true;
    }

    qDebug("xrLocateViews returned qualified success code: %s", to_string(res));
    return false;
}

void QOpenXRManager::doRender(const XrSwapchainSubImage &subImage, const XrSwapchainImageBaseHeader *swapchainImage)
{
    const int arraySize = m_multiviewRendering ? m_swapchains[0].arraySize : 1;
    m_quickWindow->setRenderTarget(m_graphics->renderTarget(subImage, swapchainImage, m_colorSwapchainFormat, m_samples, arraySize));

    m_quickWindow->setGeometry(0,
                               0,
                               subImage.imageRect.extent.width,
                               subImage.imageRect.extent.height);
    m_quickWindow->contentItem()->setSize(QSizeF(subImage.imageRect.extent.width,
                                                 subImage.imageRect.extent.height));

    m_renderControl->polishItems();
    m_renderControl->beginFrame();
    m_renderControl->sync();
    m_renderControl->render();
    m_renderControl->endFrame();

    // With multiview this indicates that the frame with both eyes is ready from
    // the 3D APIs perspective. Without multiview this is done - and so the
    // signal is emitted - multiple times (twice) per "frame" (eye).
    QRhiRenderTarget *rt = QQuickWindowPrivate::get(m_quickWindow)->activeCustomRhiRenderTarget();
    if (rt->resourceType() == QRhiResource::TextureRenderTarget) {
        QRhiTexture *colorBuffer = static_cast<QRhiTextureRenderTarget *>(rt)->description().colorAttachmentAt(0)->texture();
        emit frameReady(colorBuffer);
    }
}

void QOpenXRManager::preSetupQuickScene()
{
    m_renderControl = new QQuickRenderControl;
    m_quickWindow = new QQuickWindow(m_renderControl);
}

bool QOpenXRManager::setupQuickScene()
{
    m_graphics->setupWindow(m_quickWindow);
    m_animationDriver = new QOpenXRAnimationDriver;
    m_animationDriver->install();

    const bool initSuccess = m_renderControl->initialize();
    if (!initSuccess) {
        qWarning("Quick 3D XR: Failed to create renderControl (failed to initialize RHI?)");
        return false;
    }

    QRhi *rhi = m_renderControl->rhi();
    if (!rhi) {
        qWarning("Quick3D XR: No QRhi from renderControl. This should not happen.");
        return false;
    }

    qDebug("Quick 3D XR: QRhi initialized with backend %s", rhi->backendName());

    if (m_multiviewRendering && !rhi->isFeatureSupported(QRhi::MultiView)) {
        qWarning("Quick 3D XR: Multiview rendering was enabled, but is reported as unsupported from the current QRhi backend (%s)",
                 rhi->backendName());
        m_multiviewRendering = false;
    }

    qDebug("Quick3D XR: multiview rendering %s", m_multiviewRendering ? "enabled" : "disabled");

    return true;
}

void QOpenXRManager::updateCameraHelper(QOpenXREyeCamera *camera, const XrCompositionLayerProjectionView &layerView)
{
    camera->setAngleLeft(layerView.fov.angleLeft);
    camera->setAngleRight(layerView.fov.angleRight);
    camera->setAngleUp(layerView.fov.angleUp);
    camera->setAngleDown(layerView.fov.angleDown);

    camera->setPosition(QVector3D(layerView.pose.position.x,
                                  layerView.pose.position.y,
                                  layerView.pose.position.z) * 100.0f); // convert m to cm

    camera->setRotation(QQuaternion(layerView.pose.orientation.w,
                                    layerView.pose.orientation.x,
                                    layerView.pose.orientation.y,
                                    layerView.pose.orientation.z));
}

// Set the active camera for the view to the camera for the eye value
// This is set right before updateing/rendering for that eye's view
void QOpenXRManager::updateCameraNonMultiview(int eye, const XrCompositionLayerProjectionView &layerView)
{
    QOpenXREyeCamera *eyeCamera = m_xrOrigin ? m_xrOrigin->eyeCamera(eye) : nullptr;

    if (eyeCamera)
        updateCameraHelper(eyeCamera, layerView);

    m_vrViewport->setCamera(eyeCamera);
}

// The multiview version sets multiple cameras.
void QOpenXRManager::updateCameraMultiview(int projectionLayerViewStartIndex, int count)
{
    QVarLengthArray<QQuick3DCamera *, 4> cameras;
    for (int i = projectionLayerViewStartIndex; i < projectionLayerViewStartIndex + count; ++i) {
        QOpenXREyeCamera *eyeCamera = m_xrOrigin ? m_xrOrigin->eyeCamera(i) : nullptr;
        if (eyeCamera)
            updateCameraHelper(eyeCamera, m_projectionLayerViews[i]);
        cameras.append(eyeCamera);
    }
    m_vrViewport->setMultiViewCameras(cameras.data(), cameras.count());
}

void QOpenXRManager::checkOrigin()
{
    if (!m_xrOrigin) {
        // Check the scene for an XrOrigin
        std::function<QOpenXROrigin*(QQuick3DObject *)> findOriginNode;
        findOriginNode = [&findOriginNode](QQuick3DObject *node) -> QOpenXROrigin *{
            if (!node)
                return nullptr;
            auto origin = qobject_cast<QOpenXROrigin *>(node);
            if (origin)
                return origin;
            for (auto child : node->childItems()) {
                origin = findOriginNode(child);
                if (origin)
                    return origin;
            }
            return nullptr;
        };
        auto origin = findOriginNode(m_vrViewport->importScene());
        if (origin) {
            m_xrOrigin = origin;
            emit xrOriginChanged();
            connect(m_xrOrigin, &QObject::destroyed, this, [this](){
                m_xrOrigin = nullptr;
               emit xrOriginChanged();
            });
        }
    }
}

bool QOpenXRManager::supportsPassthrough() const
{
    XrSystemPassthroughProperties2FB passthroughSystemProperties{};
    passthroughSystemProperties.type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB;

    XrSystemProperties systemProperties{};
    systemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;
    systemProperties.next = &passthroughSystemProperties;

    XrSystemGetInfo systemGetInfo{};
    systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemGetInfo.formFactor = m_formFactor;

    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    xrGetSystem(m_instance, &systemGetInfo, &systemId);
    xrGetSystemProperties(m_instance, systemId, &systemProperties);

    bool supported = (passthroughSystemProperties.capabilities & XR_PASSTHROUGH_CAPABILITY_BIT_FB) == XR_PASSTHROUGH_CAPABILITY_BIT_FB;

    if (!supported) {
        // Try the old one. (the Simulator reports spec version 3 for
        // XR_FB_passthrough, yet the capabilities in
        // XrSystemPassthroughProperties2FB are 0)
        XrSystemPassthroughPropertiesFB oldPassthroughSystemProperties{};
        oldPassthroughSystemProperties.type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB;
        systemProperties.next = &oldPassthroughSystemProperties;
        xrGetSystemProperties(m_instance, systemId, &systemProperties);
        supported = oldPassthroughSystemProperties.supportsPassthrough;
    }

    return supported;
}

void QOpenXRManager::setupMetaQuestColorSpaces()
{
    PFN_xrEnumerateColorSpacesFB pfnxrEnumerateColorSpacesFB = NULL;
    checkXrResult(xrGetInstanceProcAddr(
        m_instance,
        "xrEnumerateColorSpacesFB",
        (PFN_xrVoidFunction*)(&pfnxrEnumerateColorSpacesFB)));

    if (!pfnxrEnumerateColorSpacesFB) // simulator
        return;

    uint32_t colorSpaceCountOutput = 0;
    checkXrResult(pfnxrEnumerateColorSpacesFB(m_session, 0, &colorSpaceCountOutput, NULL));

    XrColorSpaceFB* colorSpaces =
        (XrColorSpaceFB*)malloc(colorSpaceCountOutput * sizeof(XrColorSpaceFB));

    checkXrResult(pfnxrEnumerateColorSpacesFB(
        m_session, colorSpaceCountOutput, &colorSpaceCountOutput, colorSpaces));
    qDebug("Supported ColorSpaces:");

    for (uint32_t i = 0; i < colorSpaceCountOutput; i++) {
        qDebug("%d:%d", i, colorSpaces[i]);
    }

    const XrColorSpaceFB requestColorSpace = XR_COLOR_SPACE_QUEST_FB;

    PFN_xrSetColorSpaceFB pfnxrSetColorSpaceFB = NULL;
    checkXrResult(xrGetInstanceProcAddr(
        m_instance, "xrSetColorSpaceFB", (PFN_xrVoidFunction*)(&pfnxrSetColorSpaceFB)));

    checkXrResult(pfnxrSetColorSpaceFB(m_session, requestColorSpace));

    free(colorSpaces);
}

void QOpenXRManager::setupMetaQuestRefreshRates()
{
    PFN_xrEnumerateDisplayRefreshRatesFB pfnxrEnumerateDisplayRefreshRatesFB = NULL;
    checkXrResult(xrGetInstanceProcAddr(
        m_instance,
        "xrEnumerateDisplayRefreshRatesFB",
        (PFN_xrVoidFunction*)(&pfnxrEnumerateDisplayRefreshRatesFB)));

    if (!pfnxrEnumerateDisplayRefreshRatesFB)
        return;

    uint32_t numSupportedDisplayRefreshRates;
    QVector<float> supportedDisplayRefreshRates;

    checkXrResult(pfnxrEnumerateDisplayRefreshRatesFB(
        m_session, 0, &numSupportedDisplayRefreshRates, NULL));

    supportedDisplayRefreshRates.resize(numSupportedDisplayRefreshRates);

    checkXrResult(pfnxrEnumerateDisplayRefreshRatesFB(
        m_session,
        numSupportedDisplayRefreshRates,
        &numSupportedDisplayRefreshRates,
        supportedDisplayRefreshRates.data()));
    qDebug("Supported Refresh Rates:");
    for (uint32_t i = 0; i < numSupportedDisplayRefreshRates; i++) {
        qDebug("%d:%f", i, supportedDisplayRefreshRates[i]);
    }

    PFN_xrGetDisplayRefreshRateFB pfnGetDisplayRefreshRate;
    checkXrResult(xrGetInstanceProcAddr(
        m_instance,
        "xrGetDisplayRefreshRateFB",
        (PFN_xrVoidFunction*)(&pfnGetDisplayRefreshRate)));

    float currentDisplayRefreshRate = 0.0f;
    checkXrResult(pfnGetDisplayRefreshRate(m_session, &currentDisplayRefreshRate));
    qDebug("Current System Display Refresh Rate: %f", currentDisplayRefreshRate);

    PFN_xrRequestDisplayRefreshRateFB pfnRequestDisplayRefreshRate;
    checkXrResult(xrGetInstanceProcAddr(
        m_instance,
        "xrRequestDisplayRefreshRateFB",
        (PFN_xrVoidFunction*)(&pfnRequestDisplayRefreshRate)));

    // Test requesting the system default.
    checkXrResult(pfnRequestDisplayRefreshRate(m_session, 0.0f));
    qDebug("Requesting system default display refresh rate");
}

void QOpenXRManager::setupMetaQuestFoveation()
{
    PFN_xrCreateFoveationProfileFB pfnCreateFoveationProfileFB;
    checkXrResult(xrGetInstanceProcAddr(
        m_instance,
        "xrCreateFoveationProfileFB",
        (PFN_xrVoidFunction*)(&pfnCreateFoveationProfileFB)));

    if (!pfnCreateFoveationProfileFB) // simulator
        return;

    PFN_xrDestroyFoveationProfileFB pfnDestroyFoveationProfileFB;
    checkXrResult(xrGetInstanceProcAddr(
        m_instance,
        "xrDestroyFoveationProfileFB",
        (PFN_xrVoidFunction*)(&pfnDestroyFoveationProfileFB)));

    PFN_xrUpdateSwapchainFB pfnUpdateSwapchainFB;
    checkXrResult(xrGetInstanceProcAddr(
        m_instance, "xrUpdateSwapchainFB", (PFN_xrVoidFunction*)(&pfnUpdateSwapchainFB)));

    for (auto swapchain : m_swapchains) {
        XrFoveationLevelProfileCreateInfoFB levelProfileCreateInfo = {};
        levelProfileCreateInfo.type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB;
        levelProfileCreateInfo.level = m_foveationLevel;
        levelProfileCreateInfo.verticalOffset = 0;
        levelProfileCreateInfo.dynamic = XR_FOVEATION_DYNAMIC_DISABLED_FB;

        XrFoveationProfileCreateInfoFB profileCreateInfo = {};
        profileCreateInfo.type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB;
        profileCreateInfo.next = &levelProfileCreateInfo;

        XrFoveationProfileFB foveationProfile;
        pfnCreateFoveationProfileFB(m_session, &profileCreateInfo, &foveationProfile);

        XrSwapchainStateFoveationFB foveationUpdateState = {};
        memset(&foveationUpdateState, 0, sizeof(foveationUpdateState));
        foveationUpdateState.type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB;
        foveationUpdateState.profile = foveationProfile;

        pfnUpdateSwapchainFB(
            swapchain.handle,
            (XrSwapchainStateBaseHeaderFB*)(&foveationUpdateState));

        pfnDestroyFoveationProfileFB(foveationProfile);

        qDebug("Fixed foveated rendering requested with level %d", int(m_foveationLevel));
    }
}

void QOpenXRManager::createMetaQuestPassthrough()
{
    // According to the validation layer 'flags' cannot be 0, thus we make sure
    // this function is only ever called when we know passthrough is actually
    // enabled by the app.
    Q_ASSERT(m_passthroughSupported && m_enablePassthrough);

    qDebug() << Q_FUNC_INFO;
    PFN_xrCreatePassthroughFB pfnXrCreatePassthroughFBX = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrCreatePassthroughFB",
                                        (PFN_xrVoidFunction*)(&pfnXrCreatePassthroughFBX)));

    XrPassthroughCreateInfoFB passthroughCreateInfo{};
    passthroughCreateInfo.type = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB;
    passthroughCreateInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;

    checkXrResult(pfnXrCreatePassthroughFBX(m_session, &passthroughCreateInfo, &m_passthroughFeature));
}

void QOpenXRManager::destroyMetaQuestPassthrough()
{
    qDebug() << Q_FUNC_INFO;
    PFN_xrDestroyPassthroughFB pfnXrDestroyPassthroughFBX = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrDestroyPassthroughFB",
                                        (PFN_xrVoidFunction*)(&pfnXrDestroyPassthroughFBX)));
    checkXrResult(pfnXrDestroyPassthroughFBX(m_passthroughFeature));
    m_passthroughFeature = XR_NULL_HANDLE;
}

void QOpenXRManager::startMetaQuestPassthrough()
{
    qDebug() << Q_FUNC_INFO;
    PFN_xrPassthroughStartFB pfnXrPassthroughStartFBX = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrPassthroughStartFB",
                                        (PFN_xrVoidFunction*)(&pfnXrPassthroughStartFBX)));
    checkXrResult(pfnXrPassthroughStartFBX(m_passthroughFeature));
}

void QOpenXRManager::pauseMetaQuestPassthrough()
{
    qDebug() << Q_FUNC_INFO;
    PFN_xrPassthroughPauseFB pfnXrPassthroughPauseFBX = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrPassthroughPauseFB",
                                        (PFN_xrVoidFunction*)(&pfnXrPassthroughPauseFBX)));
    checkXrResult(pfnXrPassthroughPauseFBX(m_passthroughFeature));
}

void QOpenXRManager::createMetaQuestPassthroughLayer()
{
    qDebug() << Q_FUNC_INFO;
    PFN_xrCreatePassthroughLayerFB pfnXrCreatePassthroughLayerFBX = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrCreatePassthroughLayerFB",
                                        (PFN_xrVoidFunction*)(&pfnXrCreatePassthroughLayerFBX)));

    XrPassthroughLayerCreateInfoFB layerCreateInfo{};
    layerCreateInfo.type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB;
    layerCreateInfo.passthrough = m_passthroughFeature;
    layerCreateInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    if (m_enablePassthrough)
        layerCreateInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;

    checkXrResult(pfnXrCreatePassthroughLayerFBX(m_session, &layerCreateInfo, &m_passthroughLayer));
}

void QOpenXRManager::destroyMetaQuestPassthroughLayer()
{
    qDebug() << Q_FUNC_INFO;
    PFN_xrDestroyPassthroughLayerFB pfnXrDestroyPassthroughLayerFBX = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrDestroyPassthroughLayerFB",
                                        (PFN_xrVoidFunction*)(&pfnXrDestroyPassthroughLayerFBX)));
    checkXrResult(pfnXrDestroyPassthroughLayerFBX(m_passthroughLayer));
    m_passthroughLayer = XR_NULL_HANDLE;
}

void QOpenXRManager::pauseMetaQuestPassthroughLayer()
{
    qDebug() << Q_FUNC_INFO;
    PFN_xrPassthroughLayerPauseFB pfnXrPassthroughLayerPauseFBX = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrPassthroughLayerPauseFB",
                                        (PFN_xrVoidFunction*)(&pfnXrPassthroughLayerPauseFBX)));
    checkXrResult(pfnXrPassthroughLayerPauseFBX(m_passthroughLayer));
}

void QOpenXRManager::resumeMetaQuestPassthroughLayer()
{
    qDebug() << Q_FUNC_INFO;
    PFN_xrPassthroughLayerResumeFB pfnXrPassthroughLayerResumeFBX = nullptr;
    checkXrResult(xrGetInstanceProcAddr(m_instance,
                                        "xrPassthroughLayerResumeFB",
                                        (PFN_xrVoidFunction*)(&pfnXrPassthroughLayerResumeFBX)));
    checkXrResult(pfnXrPassthroughLayerResumeFBX(m_passthroughLayer));
}

QT_END_NAMESPACE
