import CoreGraphics
import Foundation
import SwiftUI

/// How large the app draws its own content (the node editor, the timeline, the spreadsheet, the
/// inspector), chosen in Settings ▸ General. Kept in the user defaults, so every window shares it
/// and a change applies at once.
enum InterfaceSize: String, CaseIterable, Identifiable, Sendable {
    case small
    case medium
    case large

    static let defaultsKey = "interfaceSize"
    static let `default` = InterfaceSize.medium

    var id: String { rawValue }

    var title: String {
        switch self {
        case .small: String(localized: "Small")
        case .medium: String(localized: "Medium")
        case .large: String(localized: "Large")
        }
    }

    /// The size chosen, and medium before any was chosen or when the stored value is not one of ours.
    static func stored(in defaults: UserDefaults = .standard) -> InterfaceSize {
        defaults.string(forKey: defaultsKey).flatMap(InterfaceSize.init(rawValue:)) ?? .default
    }

    func store(in defaults: UserDefaults = .standard) {
        defaults.set(rawValue, forKey: Self.defaultsKey)
    }

    /// What every size is multiplied by. Medium is the standard size of macOS.
    var scale: CGFloat {
        switch self {
        case .small: 0.9
        case .medium: 1
        case .large: 1.25
        }
    }

    /// A length of the standard size, as it is at this size, in whole points.
    func points(_ standard: CGFloat) -> CGFloat {
        (standard * scale).rounded()
    }

    /// The height of a row of the spreadsheet.
    var spreadsheetRowHeight: CGFloat { points(22) }

    /// The height of the ruler of the timeline.
    var rulerHeight: CGFloat { points(36) }

    /// The size of the controls of AppKit and SwiftUI: small, regular and large.
    var controlSize: ControlSize {
        switch self {
        case .small: .small
        case .medium: .regular
        case .large: .large
        }
    }
}

/// The text styles of the app, in points at the standard size.
enum UIText: Sendable {
    case caption, subheadline, callout, body, headline, title3

    var points: CGFloat {
        switch self {
        case .caption: 11
        case .subheadline: 12
        case .callout: 12
        case .body: 13
        case .headline: 13
        case .title3: 17
        }
    }

    var weight: Font.Weight {
        self == .headline ? .semibold : .regular
    }
}

private struct InterfaceSizeKey: EnvironmentKey {
    static let defaultValue = InterfaceSize.default
}

extension EnvironmentValues {
    /// The interface size of the window.
    var interfaceSize: InterfaceSize {
        get { self[InterfaceSizeKey.self] }
        set { self[InterfaceSizeKey.self] = newValue }
    }
}

private struct ScaledFont: ViewModifier {
    @Environment(\.interfaceSize) private var size
    let style: UIText
    let weight: Font.Weight?
    let monospaced: Bool

    func body(content: Content) -> some View {
        let font = Font.system(size: size.points(style.points), weight: weight ?? style.weight)
        content.font(monospaced ? font.monospacedDigit() : font)
    }
}

extension View {
    /// The font of a text style, at the interface size of the window.
    func uiFont(_ style: UIText, weight: Font.Weight? = nil, monospaced: Bool = false) -> some View {
        modifier(ScaledFont(style: style, weight: weight, monospaced: monospaced))
    }

    /// Makes the view follow the interface size chosen in Settings, live: the size goes in the
    /// environment, and the controls take their size from it.
    func sized() -> some View { modifier(SizedModifier()) }
}

private struct SizedModifier: ViewModifier {
    @AppStorage(InterfaceSize.defaultsKey) private var stored = InterfaceSize.default.rawValue

    func body(content: Content) -> some View {
        let size = InterfaceSize(rawValue: stored) ?? .default
        content
            .environment(\.interfaceSize, size)
            .controlSize(size.controlSize)
    }
}

/// How a count of points or triangles is written where the room is short: "1,03 M" for 1 028 781.
enum CountFormat {
    /// The count with at most three significant digits and a k or M, or in full below ten thousand.
    static func abbreviated(_ count: UInt64, locale: Locale = .current) -> String {
        if count < 10_000 { return format(Double(count), digits: 0, locale: locale) }
        let (value, suffix): (Double, String) = count < 1_000_000
            ? (Double(count) / 1_000, " k")
            : (count < 1_000_000_000 ? (Double(count) / 1_000_000, " M") : (Double(count) / 1_000_000_000, " G"))
        // Three significant digits: 12,3 and 123, 1,03 and 10,3.
        let digits = value < 10 ? 2 : (value < 100 ? 1 : 0)
        // A value that rounds up to the next power of ten (999,96 k) reads as that.
        let rounded = (value * pow(10, Double(digits))).rounded() / pow(10, Double(digits))
        if rounded >= 1000, suffix == " k" { return abbreviated(1_000_000, locale: locale) }
        return format(rounded, digits: digits, locale: locale) + suffix
    }

    private static func format(_ value: Double, digits: Int, locale: Locale) -> String {
        let formatter = NumberFormatter()
        formatter.locale = locale
        formatter.numberStyle = .decimal
        formatter.minimumFractionDigits = digits
        formatter.maximumFractionDigits = digits
        formatter.usesGroupingSeparator = false
        return formatter.string(from: NSNumber(value: value)) ?? String(Int(value))
    }

    /// "1,03 M points" or, for one, "1 point".
    static func points(_ count: UInt64) -> String {
        count == 1 ? String(localized: "1 point") : String(localized: "\(abbreviated(count)) points")
    }

    static func triangles(_ count: UInt64) -> String {
        count == 1 ? String(localized: "1 triangle") : String(localized: "\(abbreviated(count)) triangles")
    }
}
