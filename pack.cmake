if(WIN32)
    file(INSTALL DESTINATION ${PACKAGE_DIR}/bin
        TYPE FILE
        FILES
            # Dlls
            ${BIN_DIR}/Collage.dll
            ${BIN_DIR}/Equalizer.dll
            ${BIN_DIR}/EqualizerServer.dll
            ${BIN_DIR}/pthread.dll
            ${BIN_DIR}/displaySystem_Equalizer.dll)
else()
    file(INSTALL DESTINATION ${PACKAGE_DIR}/bin
        TYPE FILE
        FILES
            # Dlls
            ${BIN_DIR}/libCollage.dylib
            ${BIN_DIR}/libCollage.0.3.0.dylib
            ${BIN_DIR}/libCollage.0.3.1.dylib
            ${BIN_DIR}/libEqualizer.dylib
            ${BIN_DIR}/libEqualizer.1.0.0.dylib
            ${BIN_DIR}/libEqualizer.1.0.2.dylib
            ${BIN_DIR}/libEqualizerServer.dylib
            ${BIN_DIR}/libEqualizerServer.1.0.0.dylib
            ${BIN_DIR}/libEqualizerServer.1.0.2.dylib
            ${BIN_DIR}/libdisplaySystem_Equalizer.dylib)
endif()