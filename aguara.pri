# Settings shared by every Aguara project (core, interfaces, modules).
# Paths come from .qmake.conf.

isEmpty(AGUARA_BUILD_ROOT) {
    error("No shared build root. Open and build Aguara_POS.pro at the repository root instead of $$basename(_PRO_FILE_) on its own.")
}

# Qt 5.15 + MinGW 8.1 (32-bit) is the supported toolchain: it is the last
# combination that produces binaries that run on Windows 7.
lessThan(QT_MAJOR_VERSION, 5) | greaterThan(QT_MAJOR_VERSION, 5) {
    warning("Aguara targets Qt 5.15 (Windows 7 support). Building with Qt $$QT_VERSION.")
}

CONFIG += c++17

# The release link keeps its symbols: installer/build.sh takes them out to a file
# for the telemetry server, and strips what goes to the tills.
QMAKE_LFLAGS_RELEASE -= -Wl,-s

# Everything links against the shared interfaces library.
INCLUDEPATH += $$AGUARA_SRC_ROOT/src/interfaces
DEPENDPATH  += $$AGUARA_SRC_ROOT/src/interfaces
