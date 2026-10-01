import SwiftUI

struct Sparkline: View {
    let values: [Double]
    var body: some View {
        GeometryReader { geometry in
            Path { path in
                guard values.count > 1 else { return }
                let maximum = max(values.max() ?? 1, 1)
                for (index, value) in values.enumerated() {
                    let point = CGPoint(x: CGFloat(index) / CGFloat(values.count - 1) * geometry.size.width,
                                        y: geometry.size.height * (1 - value / maximum))
                    if index == 0 { path.move(to: point) } else { path.addLine(to: point) }
                }
            }.stroke(.teal, lineWidth: 1.5)
        }.frame(height: 32).accessibilityLabel("Histórico em memória")
    }
}
