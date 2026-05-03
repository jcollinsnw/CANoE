import SwiftUI
import WebKit

// ── Shared error view (SwiftUI — works on both platforms) ─────────────────

private struct WebErrorView: View {
    let message: String
    let onRetry: () -> Void

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: "wifi.slash")
                .font(.system(size: 44))
                .foregroundStyle(.orange)
            Text("Cannot reach node")
                .font(.headline)
                .foregroundStyle(.white)
            Text(message)
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            Button("Retry", action: onRetry)
                .buttonStyle(.bordered)
                .tint(.green)
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(red: 0.04, green: 0.06, blue: 0.08))
    }
}

// ── NodeWebView ────────────────────────────────────────────────────────────
//
// Wraps WKWebView on both iOS (UIViewRepresentable) and macOS
// (NSViewRepresentable) using conditional extensions.
//
// The shared logic (webView creation, host-change detection, GPS frame
// injection, navigation delegate, error overlay) lives on the struct and
// Coordinator directly. Only the `make*View` / `update*View` methods and the
// pull-to-refresh setup are platform-specific.

struct NodeWebView {
    let url: URL
    /// Called once after the WKWebView is created so callers can retain the
    /// Coordinator and call `sendGpsFrame(_:)` imperatively.
    var onCoordinatorReady: ((Coordinator) -> Void)?

    func makeCoordinator() -> Coordinator { Coordinator() }

    // ── Shared setup called from make*View ───────────────────────────────
    func buildWebView(coordinator: Coordinator) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.allowsInlineMediaPlayback = true

        let webView = WKWebView(frame: .zero, configuration: config)
        webView.allowsBackForwardNavigationGestures = true

#if canImport(UIKit)
        webView.isOpaque = true
        webView.backgroundColor = UIColor(red: 0.04, green: 0.06, blue: 0.08, alpha: 1)

        let refresh = UIRefreshControl()
        refresh.tintColor = UIColor(red: 0.6, green: 0.84, blue: 0.44, alpha: 1)
        refresh.addTarget(coordinator,
                          action: #selector(Coordinator.handleRefresh(_:)),
                          for: .valueChanged)
        webView.scrollView.refreshControl = refresh
#endif

        webView.navigationDelegate = coordinator
        coordinator.webView = webView
        onCoordinatorReady?(coordinator)
        webView.load(URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData,
                                timeoutInterval: 10))
        return webView
    }

    func handleUpdate(_ webView: WKWebView) {
        if webView.url?.host != url.host {
            webView.load(URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData,
                                    timeoutInterval: 10))
        }
    }
}

// ── Platform conformances ─────────────────────────────────────────────────

#if canImport(UIKit)
extension NodeWebView: UIViewRepresentable {
    func makeUIView(context: Context) -> WKWebView    { buildWebView(coordinator: context.coordinator) }
    func updateUIView(_ v: WKWebView, context: Context) { handleUpdate(v) }
}
#else
extension NodeWebView: NSViewRepresentable {
    func makeNSView(context: Context) -> WKWebView    { buildWebView(coordinator: context.coordinator) }
    func updateNSView(_ v: WKWebView, context: Context) { handleUpdate(v) }
}
#endif

// ── Coordinator ───────────────────────────────────────────────────────────

extension NodeWebView {

    @MainActor
    class Coordinator: NSObject, WKNavigationDelegate {
        weak var webView: WKWebView?

#if canImport(UIKit)
        // Pull-to-refresh (iOS only — macOS WKWebView has no scroll refreshControl)
        @objc func handleRefresh(_ sender: UIRefreshControl) {
            webView?.reload()
        }
#endif

        // MARK: - GPS frame injection

        /// Encodes `payload` as a JS array literal and calls the web UI's
        /// `sendFrame(0x305, [...])`, which POSTs to /api/send.
        func sendGpsFrame(_ payload: [UInt8]) {
            guard let webView else { return }
            let bytes = payload.map { String($0) }.joined(separator: ",")
            webView.evaluateJavaScript("sendFrame(0x305,[\(bytes)])", completionHandler: nil)
        }

        // MARK: - WKNavigationDelegate

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
#if canImport(UIKit)
            webView.scrollView.refreshControl?.endRefreshing()
#endif
            removeErrorOverlay(from: webView)
        }

        func webView(_ webView: WKWebView, didFail navigation: WKNavigation!,
                     withError error: Error) {
#if canImport(UIKit)
            webView.scrollView.refreshControl?.endRefreshing()
#endif
            showError(error, in: webView)
        }

        func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!,
                     withError error: Error) {
#if canImport(UIKit)
            webView.scrollView.refreshControl?.endRefreshing()
#endif
            showError(error, in: webView)
        }

        // MARK: - Error overlay (shared via NSHostingView / UIHostingController)

        private func showError(_ error: Error, in webView: WKWebView) {
            removeErrorOverlay(from: webView)

            let errorView = WebErrorView(message: error.localizedDescription) { [weak webView] in
                webView?.subviews.first(where: { $0.tag == 9999 })?.removeFromSuperview()
                webView?.reload()
            }

#if canImport(UIKit)
            let hosting = UIHostingController(rootView: errorView)
            hosting.view.tag = 9999
            hosting.view.translatesAutoresizingMaskIntoConstraints = false
            webView.addSubview(hosting.view)
            NSLayoutConstraint.activate([
                hosting.view.leadingAnchor.constraint(equalTo: webView.leadingAnchor),
                hosting.view.trailingAnchor.constraint(equalTo: webView.trailingAnchor),
                hosting.view.topAnchor.constraint(equalTo: webView.topAnchor),
                hosting.view.bottomAnchor.constraint(equalTo: webView.bottomAnchor),
            ])
            // Keep hosting controller alive — store in associated object
            objc_setAssociatedObject(webView, &NodeWebView.hostingKey, hosting,
                                     .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
#else
            let hosting = NSHostingView(rootView: errorView)
            hosting.tag = 9999
            hosting.translatesAutoresizingMaskIntoConstraints = false
            webView.addSubview(hosting)
            NSLayoutConstraint.activate([
                hosting.leadingAnchor.constraint(equalTo: webView.leadingAnchor),
                hosting.trailingAnchor.constraint(equalTo: webView.trailingAnchor),
                hosting.topAnchor.constraint(equalTo: webView.topAnchor),
                hosting.bottomAnchor.constraint(equalTo: webView.bottomAnchor),
            ])
#endif
        }

        private func removeErrorOverlay(from webView: WKWebView) {
            webView.subviews.first(where: { $0.tag == 9999 })?.removeFromSuperview()
#if canImport(UIKit)
            objc_setAssociatedObject(webView, &NodeWebView.hostingKey, nil,
                                     .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
#endif
        }
    }
}

// Associated object key for retaining the UIHostingController on iOS
private extension NodeWebView {
    static var hostingKey: UInt8 = 0
}
