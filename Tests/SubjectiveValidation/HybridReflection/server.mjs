import http from "node:http";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const root = path.dirname(fileURLToPath(import.meta.url));
const reportsDirectory = path.join(root, "reports");
const argumentsMap = new Map();
for (let index = 2; index + 1 < process.argv.length; index += 2)
{
    argumentsMap.set(process.argv[index], process.argv[index + 1]);
}

const port = Number(argumentsMap.get("--port") || "8765");
const shutdownToken = argumentsMap.get("--token") || "";
if (!Number.isInteger(port) || port < 1024 || port > 65535 || shutdownToken.length < 16)
{
    throw new Error("Invalid server port or shutdown token.");
}

const mimeTypes = new Map([
    [".css", "text/css; charset=utf-8"],
    [".html", "text/html; charset=utf-8"],
    [".js", "text/javascript; charset=utf-8"],
    [".json", "application/json; charset=utf-8"],
    [".png", "image/png"]
]);

function sendJson(response, status, value)
{
    const body = `${JSON.stringify(value)}\n`;
    response.writeHead(status, {
        "Content-Type": "application/json; charset=utf-8",
        "Content-Length": Buffer.byteLength(body),
        "Cache-Control": "no-store"
    });
    response.end(body);
}

function safeStaticPath(requestPath)
{
    const relativePath = decodeURIComponent(requestPath === "/" ? "/index.html" : requestPath);
    const resolved = path.resolve(root, `.${relativePath}`);
    return resolved === root || resolved.startsWith(`${root}${path.sep}`) ? resolved : null;
}

async function readRequestJson(request)
{
    const chunks = [];
    let length = 0;
    for await (const chunk of request)
    {
        length += chunk.length;
        if (length > 1024 * 1024)
        {
            throw new Error("Report exceeds 1 MiB.");
        }
        chunks.push(chunk);
    }
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

function validateReport(report)
{
    return report &&
        report.reportVersion === 1 &&
        typeof report.suiteId === "string" &&
        report.suiteId.length > 0 &&
        Number.isInteger(report.suiteVersion) &&
        typeof report.evaluatedAt === "string" &&
        Array.isArray(report.cases);
}

async function saveReport(request, response)
{
    const report = await readRequestJson(request);
    if (!validateReport(report))
    {
        sendJson(response, 400, { error: "Report does not match the required top-level contract." });
        return;
    }

    await fs.promises.mkdir(reportsDirectory, { recursive: true });
    const safeSuiteId = report.suiteId.replaceAll(/[^A-Za-z0-9_-]/g, "-");
    const timestamp = new Date().toISOString().replaceAll(":", "-");
    const suffix = crypto.randomBytes(4).toString("hex");
    const fileName = `${safeSuiteId}-report-${timestamp}-${suffix}.json`;
    const reportPath = path.join(reportsDirectory, fileName);
    await fs.promises.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, "utf8");
    sendJson(response, 201, { path: `reports/${fileName}` });
}

const server = http.createServer(async (request, response) =>
{
    try
    {
        const requestUrl = new URL(request.url, `http://${request.headers.host || "127.0.0.1"}`);
        if (request.method === "GET" && requestUrl.pathname === "/api/health")
        {
            sendJson(response, 200, { application: "hybrid-reflection-subjective-validation", version: 1 });
            return;
        }
        if (request.method === "POST" && requestUrl.pathname === "/api/report")
        {
            await saveReport(request, response);
            return;
        }
        if (request.method === "POST" && requestUrl.pathname === "/api/shutdown")
        {
            if (request.headers["x-shutdown-token"] !== shutdownToken)
            {
                sendJson(response, 403, { error: "Invalid shutdown token." });
                return;
            }
            sendJson(response, 200, { stopped: true });
            setImmediate(() => server.close());
            return;
        }
        if (request.method !== "GET")
        {
            sendJson(response, 405, { error: "Method not allowed." });
            return;
        }

        const staticPath = safeStaticPath(requestUrl.pathname);
        if (!staticPath || !fs.existsSync(staticPath) || !fs.statSync(staticPath).isFile())
        {
            sendJson(response, 404, { error: "Not found." });
            return;
        }
        const body = await fs.promises.readFile(staticPath);
        response.writeHead(200, {
            "Content-Type": mimeTypes.get(path.extname(staticPath).toLowerCase()) || "application/octet-stream",
            "Content-Length": body.length,
            "Cache-Control": "no-store"
        });
        response.end(body);
    }
    catch (error)
    {
        sendJson(response, 500, { error: error.message });
    }
});

server.listen(port, "127.0.0.1", () =>
{
    process.stdout.write(`Hybrid Reflection validation server: http://127.0.0.1:${port}/\n`);
});
