// Renders windows/src/pyaw.ico: a rounded badge with the letter မ, at the sizes Windows uses.
//   usage: make_windows_icon <out.ico> [preview.png]
import AppKit
import CoreText

let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "pyaw.ico"
let sizes = [16, 20, 24, 32, 40, 48, 64, 256]

func render(_ px: Int) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    let ctx = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = ctx
    let cg = ctx.cgContext
    let s = CGFloat(px)
    let inset = s * 0.03
    let badge = CGRect(x: inset, y: inset, width: s - 2 * inset, height: s - 2 * inset)
    cg.addPath(CGPath(roundedRect: badge, cornerWidth: s * 0.22, cornerHeight: s * 0.22, transform: nil))
    cg.setFillColor(CGColor(red: 0.07, green: 0.42, blue: 0.36, alpha: 1))  // deep jade
    cg.fillPath()
    let font = CTFontCreateWithName("Pyidaungsu-Bold" as CFString, s * 0.78, nil)
    let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor.white]
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: "မ", attributes: attrs))
    let b = CTLineGetBoundsWithOptions(line, .useGlyphPathBounds)
    cg.textPosition = CGPoint(x: badge.midX - b.midX, y: badge.midY - b.midY)
    CTLineDraw(line, cg)
    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

let images = sizes.map(render)
var ico = Data()
func u16(_ v: Int) { var x = UInt16(v).littleEndian; ico.append(Data(bytes: &x, count: 2)) }
func u32(_ v: Int) { var x = UInt32(v).littleEndian; ico.append(Data(bytes: &x, count: 4)) }
u16(0); u16(1); u16(sizes.count)
var offset = 6 + 16 * sizes.count
for (size, png) in zip(sizes, images) {
    ico.append(UInt8(size >= 256 ? 0 : size)); ico.append(UInt8(size >= 256 ? 0 : size))
    ico.append(0); ico.append(0)
    u16(1); u16(32); u32(png.count); u32(offset)
    offset += png.count
}
for png in images { ico.append(png) }
try! ico.write(to: URL(fileURLWithPath: out))
if CommandLine.arguments.count > 2 { try! images[images.count - 1].write(to: URL(fileURLWithPath: CommandLine.arguments[2])) }
print("wrote \(out) (\(ico.count) bytes)")
