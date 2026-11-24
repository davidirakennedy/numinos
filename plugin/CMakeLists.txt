# The DAW plugin: one plugin ("Numinos") that hosts every Numin.
option(NUMINOS_INSTALL_ON_BUILD "Copy the plugin into ~/Library/Audio/Plug-Ins after every build" ON)

if(APPLE)
    set(NUMINOS_FORMATS AU VST3 Standalone)
else()
    set(NUMINOS_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Numinos
    COMPANY_NAME "Numinos"
    COMPANY_WEBSITE "https://numinos.art"
    BUNDLE_ID "art.numinos.numinos"
    PRODUCT_NAME "Numinos"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Nmns   # 4 chars, at least one capital. Never change once released.
    PLUGIN_CODE Nmn1                # ditto: hosts use these to find the plugin in old sessions
    FORMATS ${NUMINOS_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    MICROPHONE_PERMISSION_ENABLED TRUE        # standalone lab can use your interface input
    COPY_PLUGIN_AFTER_BUILD ${NUMINOS_INSTALL_ON_BUILD}) # DAWs see every build immediately

target_sources(Numinos PRIVATE
    PluginProcessor.cpp PluginProcessor.h
    PluginEditor.cpp PluginEditor.h
    NuminosLook.cpp NuminosLook.h
    SamplePlayer.h)

target_compile_definitions(Numinos PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Numinos
    PRIVATE
        numins
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# The site's typefaces (EB Garamond, IM Fell English SC; SIL Open Font License, see
# plugin/fonts), embedded so the plugin looks the same on every machine.
juce_add_binary_data(NuminosFonts
    HEADER_NAME NuminosFonts.h
    NAMESPACE NuminosFonts
    SOURCES
        fonts/EBGaramond-Regular.ttf
        fonts/EBGaramond-Italic.ttf
        fonts/IMFellEnglishSC-Regular.ttf)
target_link_libraries(Numinos PRIVATE NuminosFonts)

# Each Numin's README.md (numins/<Name>/README.md, optional) is embedded so the plugin can
# show it under "About". BinaryData names come from file names, and every file is called
# README.md, so each is first copied under its Numin's folder name (Honey.md, ...).
file(GLOB NUMIN_READMES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/numins/*/README.md)
set(NUMIN_DOCS "")
foreach(readme ${NUMIN_READMES})
    get_filename_component(numinDir ${readme} DIRECTORY)
    get_filename_component(numinName ${numinDir} NAME)
    set(doc ${CMAKE_CURRENT_BINARY_DIR}/docs/${numinName}.md)
    configure_file(${readme} ${doc} COPYONLY) # re-copied whenever the README changes
    list(APPEND NUMIN_DOCS ${doc})
endforeach()
if(NUMIN_DOCS)
    juce_add_binary_data(NuminosDocs SOURCES ${NUMIN_DOCS})
    target_link_libraries(Numinos PRIVATE NuminosDocs)
    target_compile_definitions(Numinos PRIVATE NUMINOS_HAS_DOCS=1)
endif()
