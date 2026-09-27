// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "Pyaw",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "Pyaw", targets: ["PyawIME"]),
        .executable(name: "pyaw-cli", targets: ["PyawCLI"]),
        .executable(name: "build-model", targets: ["BuildModel"]),
    ],
    targets: [
        // Pure-Swift engine: Burmese text handling, romanization, language model, decoder.
        .target(name: "PyawCore"),
        // The input method app bundle executable (InputMethodKit).
        .executableTarget(
            name: "PyawIME",
            dependencies: ["PyawCore"],
            linkerSettings: [.linkedFramework("InputMethodKit"), .linkedFramework("Carbon")]
        ),
        // Command-line harness for trying and evaluating the engine.
        .executableTarget(name: "PyawCLI", dependencies: ["PyawCore"]),
        // Builds the language model and lexicon from raw corpora.
        .executableTarget(name: "BuildModel", dependencies: ["PyawCore"]),
        .testTarget(name: "PyawCoreTests", dependencies: ["PyawCore"]),
    ],
    swiftLanguageModes: [.v5]
)
