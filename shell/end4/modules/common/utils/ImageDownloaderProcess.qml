import QtQuick
import Quickshell.Windows as WinImg
import Quickshell
import Quickshell.Io
import qs.end4.modules.common
import qs.end4.modules.common.functions

Process {
    id: root

    signal done(string path, int width, int height);
    required property string filePath;
    required property string sourceUrl;
    property string downloadUserAgent: Config.options?.networking.userAgent ?? ""
    
    function processFilePath() {
        return StringUtils.shellSingleQuoteEscape(FileUtils.trimFileProtocol(filePath));
    }

    function processSourceUrl() {
        return StringUtils.shellSingleQuoteEscape(sourceUrl);
    }

    function curlUserAgentArg() {
        if (!downloadUserAgent) {
            return "";
        }
        return ` -H 'User-Agent: ${StringUtils.shellSingleQuoteEscape(downloadUserAgent)}'`;
    }

    running: true
    command: ["curl.exe", "-sSL", "--create-dirs", "-A", downloadUserAgent || "Mozilla/5.0", sourceUrl, "-o", FileUtils.trimFileProtocol(filePath)] // Windows
    onExited: { const s = WinImg.ImageTools.imageSize(FileUtils.trimFileProtocol(root.filePath)); if (s.width > 0) root.done(root.filePath, s.width, s.height); } // Windows
    stdout: StdioCollector {
        id: imageSizeOutputCollector
        onStreamFinished: {
            const output = imageSizeOutputCollector.text.trim();
            const match = output.match(/(\d+)\s*x\s*(\d+)/);

            if (match) {
                const width = Number(match[1]);
                const height = Number(match[2]);
                root.done(root.filePath, width, height);
            }
        }
    }
}
