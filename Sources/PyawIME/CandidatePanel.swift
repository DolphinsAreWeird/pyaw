import Cocoa

/// Floating candidate window shown under the text cursor while composing.
final class CandidatePanel: NSPanel {
    static let shared = CandidatePanel()

    /// Called with the index (within the visible page) of a clicked candidate.
    var onSelect: ((Int) -> Void)?

    private let effect = NSVisualEffectView()
    private let view = CandidateView()

    static let burmeseFont: NSFont = {
        for name in ["Pyidaungsu", "NotoSansMyanmar-Regular", "MyanmarSangamMN", "MyanmarMN"] {
            if let f = NSFont(name: name, size: 19) { return f }
        }
        return NSFont.systemFont(ofSize: 19)
    }()

    private init() {
        super.init(contentRect: NSRect(x: 0, y: 0, width: 200, height: 100),
                   styleMask: [.nonactivatingPanel, .borderless], backing: .buffered, defer: true)
        level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.cursorWindow)) - 1)
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true
        hidesOnDeactivate = false
        isReleasedWhenClosed = false
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient, .ignoresCycle]

        effect.material = .popover
        effect.state = .active
        effect.blendingMode = .behindWindow
        effect.wantsLayer = true
        effect.layer?.cornerRadius = 10
        effect.layer?.masksToBounds = true
        contentView = effect
        effect.addSubview(view)
        view.onClick = { [weak self] i in self?.onSelect?(i) }
    }

    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }

    struct Content {
        var input: String
        var focus: Range<Int>?
        var candidates: [String]
        var highlighted: Int?
        var page: Int
        var pageCount: Int
        var loading: Bool
        var hint: String? = nil
    }

    /// Shows the panel just below `caret` (screen coordinates of the composed text's line).
    func show(_ content: Content, caret: NSRect) {
        view.content = content
        let size = view.fittingContentSize()
        view.frame = NSRect(origin: .zero, size: size)
        effect.frame = view.frame

        let screen = NSScreen.screens.first { $0.frame.contains(caret.origin) } ?? NSScreen.main
        let visible = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        var origin = NSPoint(x: caret.minX - 8, y: caret.minY - size.height - 6)
        if origin.y < visible.minY { origin.y = caret.maxY + 6 }                 // flip above the line
        origin.x = min(max(origin.x, visible.minX + 4), visible.maxX - size.width - 4)
        origin.y = min(max(origin.y, visible.minY + 4), visible.maxY - size.height - 4)
        setFrame(NSRect(origin: origin, size: size), display: true)
        view.needsDisplay = true
        orderFrontRegardless()
    }

    func hide() { orderOut(nil) }
}

/// Draws the typed input, the numbered candidates and a page indicator.
final class CandidateView: NSView {
    var content = CandidatePanel.Content(input: "", focus: nil, candidates: [], highlighted: nil, page: 0, pageCount: 1,
                                         loading: false, hint: nil)
    var onClick: ((Int) -> Void)?

    private let padding: CGFloat = 10
    private let inputFont = NSFont.monospacedSystemFont(ofSize: 12.5, weight: .regular)
    private let numberFont = NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .medium)
    private var rowRects: [NSRect] = []

    override var isFlipped: Bool { true }

    private var rowHeight: CGFloat {
        let f = CandidatePanel.burmeseFont
        return ceil(f.ascender - f.descender) + 6
    }
    private var headerHeight: CGFloat { 26 }

    private func inputString() -> NSAttributedString {
        let s = NSMutableAttributedString(string: content.loading ? content.input + "   (loading…)" : content.input,
                                          attributes: [.font: inputFont, .foregroundColor: NSColor.secondaryLabelColor])
        if let f = content.focus, content.input.utf16.count >= f.upperBound {
            s.addAttributes([.foregroundColor: NSColor.labelColor,
                             .font: NSFont.monospacedSystemFont(ofSize: 12.5, weight: .bold)],
                            range: NSRange(location: f.lowerBound, length: f.count))
        }
        return s
    }

    private func candidateString(_ text: String, highlighted: Bool) -> NSAttributedString {
        NSAttributedString(string: text, attributes: [
            .font: CandidatePanel.burmeseFont,
            .foregroundColor: highlighted ? NSColor.white : NSColor.labelColor,
        ])
    }

    private var hasFooter: Bool { content.pageCount > 1 || content.hint != nil }

    private func footerString() -> NSAttributedString {
        NSAttributedString(string: content.hint ?? "", attributes: [
            .font: NSFont.systemFont(ofSize: 10.5), .foregroundColor: NSColor.tertiaryLabelColor,
        ])
    }

    func fittingContentSize() -> NSSize {
        var width = inputString().size().width
        for c in content.candidates {
            width = max(width, 28 + candidateString(c, highlighted: false).size().width)
        }
        width = max(width, footerString().size().width + (content.pageCount > 1 ? 44 : 0))
        width = min(width, 760)
        let footer: CGFloat = hasFooter ? 18 : 0
        let rows = CGFloat(max(content.candidates.count, 0))
        return NSSize(width: ceil(max(width, 120) + padding * 2),
                      height: ceil(headerHeight + rows * rowHeight + footer + padding * 0.8))
    }

    override func draw(_ dirtyRect: NSRect) {
        inputString().draw(at: NSPoint(x: padding, y: 7))
        // Separator under the typed input.
        NSColor.separatorColor.setFill()
        NSRect(x: padding, y: headerHeight - 3, width: bounds.width - padding * 2, height: 1).fill()

        rowRects = []
        let f = CandidatePanel.burmeseFont
        for (i, c) in content.candidates.enumerated() {
            let y = headerHeight + CGFloat(i) * rowHeight
            let row = NSRect(x: 4, y: y, width: bounds.width - 8, height: rowHeight)
            rowRects.append(row)
            let isHighlighted = content.highlighted == i
            if isHighlighted {
                NSColor.controlAccentColor.setFill()
                NSBezierPath(roundedRect: row, xRadius: 6, yRadius: 6).fill()
            }
            let number = NSAttributedString(string: "\(i + 1)", attributes: [
                .font: numberFont,
                .foregroundColor: isHighlighted ? NSColor.white.withAlphaComponent(0.85) : NSColor.tertiaryLabelColor,
            ])
            number.draw(at: NSPoint(x: padding, y: y + (rowHeight - number.size().height) / 2))
            // Baseline-centred Burmese text: Myanmar fonts have very tall ascenders.
            let textY = y + (rowHeight - (f.ascender - f.descender)) / 2
            candidateString(c, highlighted: isHighlighted).draw(at: NSPoint(x: padding + 20, y: textY))
        }
        let footerY = headerHeight + CGFloat(content.candidates.count) * rowHeight + 1
        if content.hint != nil { footerString().draw(at: NSPoint(x: padding, y: footerY)) }
        if content.pageCount > 1 {
            let label = NSAttributedString(string: "\(content.page + 1)/\(content.pageCount)  ⇟", attributes: [
                .font: NSFont.systemFont(ofSize: 10.5), .foregroundColor: NSColor.tertiaryLabelColor,
            ])
            label.draw(at: NSPoint(x: bounds.width - padding - label.size().width, y: footerY))
        }
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        if let i = rowRects.firstIndex(where: { $0.contains(p) }) { onClick?(i) }
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
}

/// Brief "English" / "မြန်မာ" badge shown near the cursor when switching modes with Shift.
final class ModeBadge: NSPanel {
    static let shared = ModeBadge()
    private let label = NSTextField(labelWithString: "")
    private var hideWork: DispatchWorkItem?

    private init() {
        super.init(contentRect: NSRect(x: 0, y: 0, width: 80, height: 30),
                   styleMask: [.nonactivatingPanel, .borderless], backing: .buffered, defer: true)
        level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.cursorWindow)) - 1)
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true
        ignoresMouseEvents = true
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient, .ignoresCycle]
        let effect = NSVisualEffectView()
        effect.material = .hudWindow
        effect.state = .active
        effect.wantsLayer = true
        effect.layer?.cornerRadius = 8
        effect.layer?.masksToBounds = true
        contentView = effect
        label.alignment = .center
        effect.addSubview(label)
    }

    func flash(_ text: String, burmese: Bool, near caret: NSRect) {
        label.font = burmese ? NSFont(name: CandidatePanel.burmeseFont.fontName, size: 15) : .systemFont(ofSize: 13, weight: .semibold)
        label.stringValue = text
        label.sizeToFit()
        let size = NSSize(width: label.frame.width + 24, height: max(label.frame.height + 10, 28))
        label.frame = NSRect(x: 12, y: (size.height - label.frame.height) / 2, width: label.frame.width, height: label.frame.height)
        contentView?.frame = NSRect(origin: .zero, size: size)
        let origin = caret == .zero ? NSEvent.mouseLocation : NSPoint(x: caret.minX, y: caret.minY - size.height - 6)
        setFrame(NSRect(origin: origin, size: size), display: true)
        orderFrontRegardless()
        hideWork?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.orderOut(nil) }
        hideWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.9, execute: work)
    }
}
