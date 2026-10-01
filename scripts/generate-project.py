#!/usr/bin/python3
"""Deterministic Xcode project; development-only, uses the CLT Python standard library."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
project = root / 'LocalVoiceTranslator.xcodeproj'
project.mkdir(exist_ok=True)
objects = {}

def uid(name):
    return hashlib.sha256(name.encode()).hexdigest()[:24].upper()

def add(name, value):
    ident = uid(name)
    objects[ident] = value
    return ident

def q(value):
    return json.dumps(str(value))

def array(values):
    return '(' + ', '.join(values) + ', )' if values else '()'

sources = sorted(str(p.relative_to(root)) for folder in ['app', 'bridge', 'engine', 'third_party/webrtcvad']
                 for p in (root / folder).rglob('*') if p.suffix in ['.swift', '.mm', '.cpp', '.c'])
refs, builds = [], []
for path in sources:
    file_type = {'.swift': 'sourcecode.swift', '.mm': 'sourcecode.cpp.objcpp', '.cpp': 'sourcecode.cpp.cpp', '.c': 'sourcecode.c.c'}[Path(path).suffix]
    ref = add('file:' + path, f'isa = PBXFileReference; lastKnownFileType = {file_type}; path = {q(path)}; sourceTree = SOURCE_ROOT;')
    refs.append(ref)
    builds.append(add('build:' + path, f'isa = PBXBuildFile; fileRef = {ref};'))
app_ref = add('app-product', 'isa = PBXFileReference; explicitFileType = wrapper.application; path = LocalVoiceTranslator.app; sourceTree = BUILT_PRODUCTS_DIR;')
test_ref = add('test-product', 'isa = PBXFileReference; explicitFileType = wrapper.cfbundle; path = LocalVoiceTranslatorTests.xctest; sourceTree = BUILT_PRODUCTS_DIR;')
test_core_builds = []
for source in sorted((root / 'tests/swift').glob('*Tests.swift')):
    if source.name == 'NativeSessionTests.swift':
        continue  # Standalone @main diagnostic, not an XCTest source.
    test_path = str(source.relative_to(root))
    test_source_ref = add('file:' + test_path, f'isa = PBXFileReference; lastKnownFileType = sourcecode.swift; path = {q(test_path)}; sourceTree = SOURCE_ROOT;')
    refs.append(test_source_ref)
    test_core_builds.append(add('build:' + test_path, f'isa = PBXBuildFile; fileRef = {test_source_ref};'))
for path in [p for p in sources if p in ('app/AppState.swift', 'app/PerformanceRecording.swift', 'app/ModelCatalog.swift', 'app/AudioRouting.swift', 'app/NativeCallAudio.mm', 'app/DirectionSettings.swift', 'app/GlossaryStore.swift') or p.startswith(('bridge/', 'engine/', 'third_party/'))]:
    test_core_builds.append(add('test-build:' + path, f'isa = PBXBuildFile; fileRef = {uid("file:" + path)};'))
resource_builds = []
for path in ['build/ModelResources'] + sorted(str(p.relative_to(root)) for p in (root / 'app/Resources').iterdir() if p.suffix in ('.txt', '.json')):
    kind = 'folder' if path == 'build/ModelResources' else 'text'
    ref = add('resource:' + path, f'isa = PBXFileReference; lastKnownFileType = {kind}; path = {q(path)}; sourceTree = SOURCE_ROOT;')
    refs.append(ref)
    resource_builds.append(add('resource-build:' + path, f'isa = PBXBuildFile; fileRef = {ref};'))
products = add('products', f'isa = PBXGroup; children = {array([app_ref, test_ref])}; name = Products; sourceTree = "<group>";')
main_group = add('main-group', f'isa = PBXGroup; children = {array(refs + [products])}; sourceTree = "<group>";')
source_phase = add('sources', f'isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = {array(builds)}; runOnlyForDeploymentPostprocessing = 0;')
frameworks = add('frameworks', 'isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0;')
resources = add('resources', f'isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = {array(resource_builds)}; runOnlyForDeploymentPostprocessing = 0;')
test_phase = add('test-sources', f'isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = {array(test_core_builds)}; runOnlyForDeploymentPostprocessing = 0;')
configs = {}
for target in ['project', 'app', 'tests']:
    ids = []
    for config in ['Debug', 'Release']:
        settings = {
            'SDKROOT': 'macosx', 'MACOSX_DEPLOYMENT_TARGET': '14.0', 'ARCHS': 'arm64',
            'CLANG_CXX_LANGUAGE_STANDARD': 'c++20', 'CLANG_CXX_LIBRARY': 'libc++',
            'CLANG_ENABLE_MODULES': 'YES', 'CLANG_ENABLE_OBJC_ARC': 'YES',
            'SWIFT_VERSION': '5.0', 'ENABLE_USER_SCRIPT_SANDBOXING': 'YES',
            'SWIFT_ACTIVE_COMPILATION_CONDITIONS': 'DEBUG' if config == 'Debug' else '',
            'CODE_SIGN_IDENTITY': '-', 'CODE_SIGN_STYLE': 'Manual',
            'SWIFT_OPTIMIZATION_LEVEL': '-Onone' if config == 'Debug' else '-O',
            'GCC_OPTIMIZATION_LEVEL': '0' if config == 'Debug' else '3',
            'ENABLE_TESTABILITY': 'YES' if config == 'Debug' else 'NO',
            'ENABLE_DEBUG_DYLIB': 'NO',
            'DEBUG_INFORMATION_FORMAT': 'dwarf',
            'CLANG_MODULE_CACHE_PATH': '$(SRCROOT)/cache/clang',
            'SWIFT_MODULE_CACHE_PATH': '$(SRCROOT)/cache/swift',
            'MODULE_CACHE_DIR': '$(SRCROOT)/cache/modules',
            'GCC_PREPROCESSOR_DEFINITIONS': '$(inherited) WEBRTC_POSIX',
            'HEADER_SEARCH_PATHS': '$(SRCROOT)/engine $(SRCROOT)/third_party/webrtcvad $(SRCROOT)/third_party/whisper.cpp/include $(SRCROOT)/third_party/whisper.cpp/ggml/include $(SRCROOT)/third_party/ctranslate2/include $(SRCROOT)/third_party/sentencepiece/src $(SRCROOT)/third_party/sherpa-onnx $(SRCROOT)/build/sherpa/_deps/onnxruntime-src/include $(SRCROOT)/build/sherpa/_deps/kaldi_native_fbank-src',
            'LIBRARY_SEARCH_PATHS': '$(SRCROOT)/build/whisper/lib $(SRCROOT)/build/translation/lib $(SRCROOT)/build/sherpa/lib',
        }
        if target == 'app':
            settings.update({'PRODUCT_NAME': 'LocalVoiceTranslator', 'PRODUCT_BUNDLE_IDENTIFIER': 'local.LocalVoiceTranslator',
                             'INFOPLIST_FILE': 'app/Info.plist', 'CODE_SIGN_ENTITLEMENTS': 'app/LocalVoiceTranslator.entitlements',
                             'ENABLE_APP_SANDBOX': 'YES', 'SWIFT_OBJC_BRIDGING_HEADER': 'app/AppBridge.h',
                             'OTHER_LDFLAGS': '-framework AVFoundation -framework AudioToolbox -framework CoreAudio -framework Metal -framework MetalKit -framework Accelerate -lwhisper -lwhisper.coreml -framework CoreML -lggml -lggml-cpu -lggml-metal -lggml-blas -lggml-base -ltranslation-deps -lspeech-deps',
                             'LD_RUNPATH_SEARCH_PATHS': '$(inherited) @executable_path/../Frameworks'})
        elif target == 'tests':
            settings.update({'PRODUCT_NAME': 'LocalVoiceTranslatorTests', 'PRODUCT_BUNDLE_IDENTIFIER': 'local.LocalVoiceTranslator.tests',
                             'GENERATE_INFOPLIST_FILE': 'YES',
                             'SWIFT_OBJC_BRIDGING_HEADER': 'app/AppBridge.h',
                             'OTHER_LDFLAGS': '-framework AVFoundation -framework AudioToolbox -framework CoreAudio -framework Metal -framework MetalKit -framework Accelerate -lwhisper -lwhisper.coreml -framework CoreML -lggml -lggml-cpu -lggml-metal -lggml-blas -lggml-base -ltranslation-deps -lspeech-deps',
                             'LD_RUNPATH_SEARCH_PATHS': '$(inherited) @executable_path/../Frameworks @loader_path/../Frameworks'})
        body = ' '.join(f'{key} = {q(value)};' for key, value in settings.items())
        ids.append(add(target + ':' + config, f'isa = XCBuildConfiguration; buildSettings = {{ {body} }}; name = {config};'))
    configs[target] = add(target + '-configs', f'isa = XCConfigurationList; buildConfigurations = {array(ids)}; defaultConfigurationIsVisible = 0; defaultConfigurationName = Release;')
app_target = add('app-target', f'isa = PBXNativeTarget; buildConfigurationList = {configs["app"]}; buildPhases = {array([source_phase, frameworks, resources])}; buildRules = (); dependencies = (); name = LocalVoiceTranslator; productName = LocalVoiceTranslator; productReference = {app_ref}; productType = "com.apple.product-type.application";')
proxy = add('test-proxy', f'isa = PBXContainerItemProxy; containerPortal = {uid("project")}; proxyType = 1; remoteGlobalIDString = {app_target}; remoteInfo = LocalVoiceTranslator;')
dependency = add('test-dependency', f'isa = PBXTargetDependency; target = {app_target}; targetProxy = {proxy};')
test_target = add('test-target', f'isa = PBXNativeTarget; buildConfigurationList = {configs["tests"]}; buildPhases = {array([test_phase])}; buildRules = (); dependencies = {array([dependency])}; name = LocalVoiceTranslatorTests; productName = LocalVoiceTranslatorTests; productReference = {test_ref}; productType = "com.apple.product-type.bundle.unit-test";')
project_id = add('project', f'isa = PBXProject; attributes = {{ BuildIndependentTargetsInParallel = YES; LastUpgradeCheck = 2650; }}; buildConfigurationList = {configs["project"]}; compatibilityVersion = "Xcode 14.0"; developmentRegion = en; knownRegions = (en, Base); mainGroup = {main_group}; productRefGroup = {products}; projectDirPath = ""; projectRoot = ""; targets = {array([app_target, test_target])};')
text = '// !$*UTF8*$!\n{ archiveVersion = 1; classes = {}; objectVersion = 56; objects = {\n'
text += '\n'.join(f'{key} = {{ {value} }};' for key, value in objects.items())
text += f'\n}}; rootObject = {project_id}; }}\n'
(project / 'project.pbxproj').write_text(text)
scheme_dir = project / 'xcshareddata/xcschemes'
scheme_dir.mkdir(parents=True, exist_ok=True)
def reference(ident, name, product):
    return f'<BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="{ident}" BuildableName="{product}" BlueprintName="{name}" ReferencedContainer="container:LocalVoiceTranslator.xcodeproj"/>'
app = reference(app_target, 'LocalVoiceTranslator', 'LocalVoiceTranslator.app')
tests = reference(test_target, 'LocalVoiceTranslatorTests', 'LocalVoiceTranslatorTests.xctest')
(scheme_dir / 'LocalVoiceTranslator.xcscheme').write_text(f'''<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion="2650" version="1.7">
<BuildAction parallelizeBuildables="YES" buildImplicitDependencies="YES"><BuildActionEntries>
<BuildActionEntry buildForTesting="YES" buildForRunning="YES" buildForProfiling="YES" buildForArchiving="YES" buildForAnalyzing="YES">{app}</BuildActionEntry>
</BuildActionEntries></BuildAction>
<TestAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.IDEFoundation.Launcher.LLDB" shouldUseLaunchSchemeArgsEnv="YES"><Testables><TestableReference skipped="NO">{tests}</TestableReference></Testables><MacroExpansion>{app}</MacroExpansion></TestAction>
<LaunchAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.IDEFoundation.Launcher.LLDB" launchStyle="0" useCustomWorkingDirectory="YES" customWorkingDirectory="{root}" ignoresPersistentStateOnLaunch="YES" debugDocumentVersioning="NO" allowLocationSimulation="NO"><BuildableProductRunnable runnableDebuggingMode="0">{app}</BuildableProductRunnable></LaunchAction>
<ProfileAction buildConfiguration="Release" shouldUseLaunchSchemeArgsEnv="YES" useCustomWorkingDirectory="NO"><BuildableProductRunnable runnableDebuggingMode="0">{app}</BuildableProductRunnable></ProfileAction>
<AnalyzeAction buildConfiguration="Debug"/><ArchiveAction buildConfiguration="Release" revealArchiveInOrganizer="YES"/>
</Scheme>''')
print('Generated LocalVoiceTranslator.xcodeproj')
