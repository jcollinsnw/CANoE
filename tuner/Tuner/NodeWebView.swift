import SwiftUI
import WebKit

/// A full-screen WKWebView that loads the ESP32 node's built-in web UI.
/// Pull-to-refresh reloads the page. Navigation errors show an inline retry view.
///
/// GPS injection: call `coordinator.sendGpsFrame(_:)` with a 5-byte GPS_DATA
/// payload to invoke the web UI's existing `sendFrame(0x305, [...])` function,
/// which POSTs to /api/send exactly as the hardware GPS module would.
struct NodeWebView: UIViewRepresentable {
    let url: URL
    /// Called once after the WKWebView is created, passing back the Coordinator
    /// so callers can invoke `sendGpsFrame(_:)` imperatively.
    var onCoordinatorReady: ((Coordinator) -> Void)?

    func makeUIView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.allowsInlineMediaPlayback = true

        let webView = WKWebView(frame: .zero, configuration: config)
        webView.allowsBackForwardNavigationGestures = true
        webView.isOpaque = true
        webView.backgroundColor = UIColor(red: 0.04, green: 0.06, blue: 0.08, alpha: 1) // match ESP32 UI bg

        // Pull-to-refresh
        let refresh = UIRefreshControl()
        refresh.tintColor = UIColor(red: 0.6, green: 0.84, blue: 0.44, alpha: 1) // #9bd770
        refresh.addTarget(context.coordinator, action: #selector(Coordinator.handleRefresh(_:)), for: .valueChanged)
        webView.scrollView.refreshControl = refresh

        webView.navigationDelegate = context.coordinator
        context.coordinator.webView = webView
        onCoordinatorReady?(context.coordinator)

        webView.load(URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 10))
        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {
        // If the target host changed (user reconnected to a different node), reload.
        if webView.url?.host != url.host {
            webView.load(URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 10))
        }
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    // MARK: - Coordinator

    @MainActor
    class Coordinator: NSObject, WKNavigationDelegate {
        weak var webView: WKWebView?

        @objc func handleRefresh(_ sender: UIRefreshControl) {
            webView?.reload()
            // endRefreshing is called from webView(_:didFinish:) / webView(_:didFail:)
        }

        // MARK: - GPS frame injection

        /// Encodes `payload` as a JS array literal and calls the web UI's
        /// existing `sendFrame(0x305, [...])` function, which POSTs it to /api/send.
        func sendGpsFrame(_ payload: [UInt8]) {
            guard let webView else { return }
            let bytes = payload.map { String($0) }.joined(separator: ",")
            webView.evaluateJavaScript("sendFrame(0x305,[\(bytes)])", completionHandler: nil)
        }

        // MARK: WKNavigationDelegate

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            webView.scrollView.refreshControl?.endRefreshing()
            removeErrorOverlay(from: webView)
        }

        func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
            webView.scrollView.refreshControl?.endRefreshing()
            showError(error, in: webView)
        }

        func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
            webView.scrollView.refreshControl?.endRefreshing()
            showError(error, in: webView)
        }

        // MARK: - Error overlay

        private func showError(_ error: Error, in webView: WKWebView) {
            removeErrorOverlay(from: webView)

            let container = UIView()
            container.tag = 9999
            container.backgroundColor = UIColor(red: 0.04, green: 0.06, blue: 0.08, alpha: 1)
            container.translatesAutoresizingMaskIntoConstraints = false

            let stack = UIStackView()
            stack.axis = .vertical
            stack.alignment = .center
            stack.spacing = 12
            stack.translatesAutoresizingMaskIntoConstraints = false

            let icon = UIImageView(image: UIImage(systemName: "wifi.slash"))
            icon.tintColor = UIColor(red: 0.95, green: 0.6, blue: 0.0, alpha: 1)
            icon.preferredSymbolConfiguration = .init(pointSize: 44)

            let title = UILabel()
            title.text = "Cannot reach node"
            title.font = .boldSystemFont(ofSize: 18)
            title.textColor = .white

            let detail = UILabel()
            detail.text = error.localizedDescription
            detail.font = .systemFont(ofSize: 13)
            detail.textColor = UIColor(white: 0.55, alpha: 1)
            detail.textAlignment = .center
            detail.numberOfLines = 3

            let retry = UIButton(configuration: .filled())
            retry.setTitle("Retry", for: .normal)
            retry.configuration?.baseBackgroundColor = UIColor(red: 0.1, green: 0.3, blue: 0.1, alpha: 1)
            retry.configuration?.baseForegroundColor = UIColor(red: 0.6, green: 0.84, blue: 0.44, alpha: 1)
            retry.addTarget(self, action: #selector(retryTapped), for: .touchUpInside)

            [icon, title, detail, retry].forEach { stack.addArrangedSubview($0) }
            container.addSubview(stack)

            NSLayoutConstraint.activate([
                stack.centerXAnchor.constraint(equalTo: container.centerXAnchor),
                stack.centerYAnchor.constraint(equalTo: container.centerYAnchor),
                stack.leadingAnchor.constraint(greaterThanOrEqualTo: container.leadingAnchor, constant: 24),
                stack.trailingAnchor.constraint(lessThanOrEqualTo: container.trailingAnchor, constant: -24),
            ])

            webView.addSubview(container)
            NSLayoutConstraint.activate([
                container.leadingAnchor.constraint(equalTo: webView.leadingAnchor),
                container.trailingAnchor.constraint(equalTo: webView.trailingAnchor),
                container.topAnchor.constraint(equalTo: webView.topAnchor),
                container.bottomAnchor.constraint(equalTo: webView.bottomAnchor),
            ])
        }

        private func removeErrorOverlay(from webView: WKWebView) {
            webView.subviews.first(where: { $0.tag == 9999 })?.removeFromSuperview()
        }

        @objc private func retryTapped() {
            guard let webView else { return }
            removeErrorOverlay(from: webView)
            webView.reload()
        }
    }
}
