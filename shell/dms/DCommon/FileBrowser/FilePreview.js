.pragma library

const textApplications = /^application\/(json|xml|javascript|ecmascript|x-javascript|x-sh|x-shellscript|x-csh|x-perl|x-python|x-ruby|x-php|sql|toml|yaml|x-yaml|x-desktop|x-ini|x-subrip|x-trash|x-awk|x-tex|pgp-keys|pgp-signature|x-pem-file|x-x509-ca-cert|x-zerosize)$|^application\/.+\+(json|xml)$/;

const thumbnailSizes = [["normal", 128], ["large", 256], ["x-large", 512], ["xx-large", 1024]];

function isText(mime) {
    return mime.startsWith("text/") || textApplications.test(mime);
}

function viewerFor(entry, imageSizeLimit) {
    if (!entry)
        return "";
    if (entry.isDir)
        return "folder";
    if (entry.unreadable || entry.symlinkBroken)
        return "";
    const mime = entry.mime ?? "";
    if (mime === "image/svg+xml")
        return "svg";
    if (mime.startsWith("image/"))
        return imageSizeLimit < 0 || entry.size <= imageSizeLimit ? "image" : "";
    if (isText(mime))
        return "text";
    return entry.thumbnailable ? "thumbnail" : "";
}

function readsAllowed(directory) {
    return directory?.pollOnFocus !== true || directory?.networkThumbnails === true;
}

function drawerWidth(available, preferred, minimum, viewMinimum, ratio) {
    const room = available - viewMinimum;
    if (room < minimum)
        return 0;
    const wanted = preferred > 0 ? preferred : available * ratio;
    return Math.round(Math.max(minimum, Math.min(room, wanted)));
}

function thumbnailSizeFor(pixels) {
    const fit = thumbnailSizes.find(size => pixels <= size[1]);
    return (fit ?? thumbnailSizes[thumbnailSizes.length - 1])[0];
}

function chunkEnd(line, start, chunk) {
    const end = Math.min(line.length, start + chunk);
    if (end >= line.length)
        return end;
    const last = line.charCodeAt(end - 1);
    const splitsPair = last >= 0xD800 && last <= 0xDBFF && end - 1 > start;
    return splitsPair ? end - 1 : end;
}

function textLines(text, chunk) {
    const lines = [];
    for (const raw of text.split("\n")) {
        const line = raw.endsWith("\r") ? raw.slice(0, -1) : raw;
        if (line.length <= chunk) {
            lines.push(line);
            continue;
        }
        for (let start = 0; start < line.length; ) {
            const end = chunkEnd(line, start, chunk);
            lines.push(line.slice(start, end));
            start = end;
        }
    }
    if (lines.length > 1 && lines[lines.length - 1] === "")
        lines.pop();
    return lines;
}
