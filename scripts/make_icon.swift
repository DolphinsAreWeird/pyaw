// Renders the menu-bar icon: a rounded badge with the letter မ knocked out, as a template TIFF
// with 1x and 2x representations.   usage: make_icon <out.tiff> [preview.png]
import AppKit
import CoreText

let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "MenuIcon.tiff"
let size = NSSize(width: 16, height: 16)
let image = NSImage(size: size)

func render(scale: Int) -> NSBitmapImageRep {
    let px = Int(size.width) * scale
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = size
    let ctx = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = ctx
    let cg = ctx.cgContext

    let badge = CGRect(x: 0.5, y: 1, width: 15, height: 14)
    cg.addPath(CGPath(roundedRect: badge, cornerWidth: 3.4, cornerHeight: 3.4, transform: nil))
    cg.setFillColor(NSColor.black.cgColor)
    cg.fillPath()

    // Knock the glyph out, centred on its actual outline bounds.
    let font = CTFontCreateWithName(("Pyidaungsu-Bold" as CFString), 13.5, nil)
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: "မ", attributes: [.font: font]))
    let bounds = CTLineGetBoundsWithOptions(line, .useGlyphPathBounds)
    cg.setBlendMode(.destinationOut)
    cg.textPosition = CGPoint(x: badge.midX - bounds.midX, y: badge.midY - bounds.midY)
    CTLineDraw(line, cg)
    NSGraphicsContext.restoreGraphicsState()
    return rep
}

let reps = [render(scale: 1), render(scale: 2)]
reps.forEach { image.addRepresentation($0) }
image.isTemplate = true
try! image.tiffRepresentation(using: .lzw, factor: 1)!.write(to: URL(fileURLWithPath: out))
if CommandLine.arguments.count > 2 {
    // The 2x bitmap as PNG, for checking the design.
    try! reps[1].representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
}
print("wrote \(out)")
