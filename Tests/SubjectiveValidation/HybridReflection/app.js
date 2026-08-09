"use strict";

const reportVersion = 1;
const defectValues = [
    "noise",
    "silhouette-trail",
    "internal-trail",
    "flicker",
    "screen-edge",
    "brightness-detail-loss"
];
const uiText = {
    en: {
        pageEyebrow: "RtPbrSurvey visual validation",
        pageTitle: "Hybrid Reflection",
        languageButton: "日本語",
        saveReport: "Save report.json",
        savingReport: "Saving report...",
        savedReport: path => `Saved ${path}`,
        saveFallback: error => `Server save failed; downloaded a local copy instead. ${error}`,
        progress: (answered, total) => `${answered} / ${total} answered`,
        complete: "Complete",
        comparison: "A and B image comparison",
        pass: "Pass",
        fail: "Fail",
        unable: "Unable to judge",
        defectsTitle: "Observed defects (optional)",
        defects: {
            "noise": "Residual noise",
            "silhouette-trail": "Silhouette trail",
            "internal-trail": "Internal-detail trail",
            "flicker": "Flicker",
            "screen-edge": "Screen-edge artifact",
            "brightness-detail-loss": "Brightness/detail loss"
        },
        notesTitle: "Notes (optional)",
        notesPlaceholder: "Record where and when the artifact is visible.",
        missingCapture: "Capture missing",
        loadError: error =>
            `Unable to load the validation suite. Serve this directory over HTTP and verify the suite path. ${error}`,
        unavailable: "Validation suite unavailable.",
        metadata: {}
    },
    ja: {
        pageEyebrow: "RtPbrSurvey 視覚評価",
        pageTitle: "ハイブリッドリフレクション",
        languageButton: "English",
        saveReport: "report.json を保存",
        savingReport: "レポートを保存しています...",
        savedReport: path => `保存しました: ${path}`,
        saveFallback: error => `サーバー保存に失敗したため、ローカルへダウンロードしました。${error}`,
        progress: (answered, total) => `${answered} / ${total} 回答済み`,
        complete: "完了",
        comparison: "A/B画像比較",
        pass: "合格",
        fail: "不合格",
        unable: "判定不能",
        defectsTitle: "観測した問題（任意）",
        defects: {
            "noise": "残留ノイズ",
            "silhouette-trail": "輪郭の残像",
            "internal-trail": "内部ディテールの残像",
            "flicker": "フリッカー",
            "screen-edge": "画面端のアーティファクト",
            "brightness-detail-loss": "明るさ／ディテールの損失"
        },
        notesTitle: "メモ（任意）",
        notesPlaceholder: "問題が見える場所とタイミングを記録してください。",
        missingCapture: "画像がありません",
        loadError: error => `評価suiteを読み込めません。HTTP配信とsuite pathを確認してください。${error}`,
        unavailable: "評価suiteを利用できません。",
        metadata: {
            commit: "コミット",
            scene: "シーン",
            debugView: "デバッグ表示",
            noiseStrength: "ノイズ強度",
            aHistoryWeight: "A 履歴重み",
            bHistoryWeight: "B 履歴重み",
            capturedAtUtc: "撮影UTC",
            capturePlan: "撮影plan",
            capturePlanSha256: "撮影plan SHA-256",
            workingTreeDirty: "未コミット差分"
        }
    }
};

const caseList = document.querySelector("#case-list");
const caseTemplate = document.querySelector("#case-template");
const description = document.querySelector("#suite-description");
const metadata = document.querySelector("#suite-metadata");
const progress = document.querySelector("#progress");
const exportButton = document.querySelector("#export-report");
const errorPanel = document.querySelector("#suite-error");
const reportStatus = document.querySelector("#report-status");
const languageButton = document.querySelector("#toggle-language");

let suite = null;
let activeLocale = "en";

function querySuitePath()
{
    const parameters = new URLSearchParams(window.location.search);
    return parameters.get("suite") || "suite.json";
}

function makeRadioOption(caseId, criterionId, value)
{
    const option = document.createElement("label");
    const input = document.createElement("input");
    const label = document.createElement("span");
    input.type = "radio";
    input.name = `${caseId}.${criterionId}`;
    input.value = value;
    label.dataset.choiceValue = value;
    input.addEventListener("change", updateProgress);
    option.append(input, label);
    return option;
}

function makeCriterion(testCase, criterion)
{
    const fieldset = document.createElement("fieldset");
    fieldset.className = "criterion";
    fieldset.dataset.criterionId = criterion.id;

    const legend = document.createElement("legend");
    legend.textContent = criterion.prompt;
    const options = document.createElement("div");
    options.className = "radio-options";
    options.append(
        makeRadioOption(testCase.id, criterion.id, "pass"),
        makeRadioOption(testCase.id, criterion.id, "fail"),
        makeRadioOption(testCase.id, criterion.id, "unable")
    );
    fieldset.append(legend, options);
    return fieldset;
}

function configureImage(image, path, alt)
{
    image.src = path;
    image.alt = alt;
    image.addEventListener("error", () => image.classList.add("missing"));
}

function renderCase(testCase)
{
    const fragment = caseTemplate.content.cloneNode(true);
    const article = fragment.querySelector(".test-case");
    article.dataset.caseId = testCase.id;
    fragment.querySelector(".case-id").textContent = testCase.id;
    fragment.querySelector(".case-title").textContent = testCase.title;
    fragment.querySelector(".case-description").textContent = testCase.description;
    fragment.querySelector(".image-a-label").textContent = testCase.images.a.label;
    fragment.querySelector(".image-b-label").textContent = testCase.images.b.label;
    configureImage(fragment.querySelector(".image-a"), testCase.images.a.path, `${testCase.title}, A`);
    configureImage(fragment.querySelector(".image-b"), testCase.images.b.path, `${testCase.title}, B`);

    const criteria = fragment.querySelector(".criteria");
    testCase.criteria.forEach(criterion => criteria.append(makeCriterion(testCase, criterion)));

    const defectContainer = fragment.querySelector(".defect-options");
    defectValues.forEach(value =>
    {
        const option = document.createElement("label");
        const input = document.createElement("input");
        const label = document.createElement("span");
        input.type = "checkbox";
        input.value = value;
        label.dataset.defectValue = value;
        option.append(input, label);
        defectContainer.append(option);
    });

    caseList.append(fragment);
}

function renderSuite(loadedSuite)
{
    suite = loadedSuite;
    metadata.innerHTML = "";
    Object.entries(suite.capture).forEach(([key, value]) =>
    {
        const item = document.createElement("span");
        const heading = document.createElement("strong");
        item.dataset.metadataKey = key;
        item.append(heading, String(value));
        metadata.append(item);
    });
    suite.cases.forEach(renderCase);
    exportButton.disabled = false;
    applyLocale();
}

function localizedCase(testCase)
{
    return suite.locales?.[activeLocale]?.cases?.[testCase.id] || testCase;
}

function applyLocale()
{
    const text = uiText[activeLocale];
    const localizedSuite = suite.locales?.[activeLocale] || suite;
    document.documentElement.lang = activeLocale;
    document.title = `${localizedSuite.title} - ${text.pageTitle}`;
    document.querySelector("#page-eyebrow").textContent = text.pageEyebrow;
    document.querySelector("#page-title").textContent = text.pageTitle;
    description.textContent = localizedSuite.description;
    languageButton.textContent = text.languageButton;
    exportButton.textContent = text.saveReport;
    reportStatus.textContent = "";
    reportStatus.classList.remove("error");

    metadata.querySelectorAll("[data-metadata-key]").forEach(item =>
    {
        const key = item.dataset.metadataKey;
        item.querySelector("strong").textContent = `${text.metadata[key] || key}: `;
    });

    caseList.querySelectorAll(".test-case").forEach(article =>
    {
        const testCase = suite.cases.find(value => value.id === article.dataset.caseId);
        const localized = localizedCase(testCase);
        article.querySelector(".case-title").textContent = localized.title;
        article.querySelector(".case-description").textContent = localized.description;
        article.querySelector(".image-a-label").textContent = localized.images?.a?.label || testCase.images.a.label;
        article.querySelector(".image-b-label").textContent = localized.images?.b?.label || testCase.images.b.label;
        article.querySelector(".comparison").setAttribute("aria-label", text.comparison);
        article.querySelector(".image-a").alt = `${localized.title}, A`;
        article.querySelector(".image-b").alt = `${localized.title}, B`;
        article.querySelectorAll(".image-frame").forEach(frame => frame.dataset.missingLabel = text.missingCapture);
        article.querySelectorAll(".criterion").forEach(criterion =>
        {
            const criterionId = criterion.dataset.criterionId;
            const baseCriterion = testCase.criteria.find(value => value.id === criterionId);
            criterion.querySelector("legend").textContent = localized.criteria?.[criterionId] || baseCriterion.prompt;
        });
        article.querySelectorAll("[data-choice-value]").forEach(label =>
        {
            label.textContent = text[label.dataset.choiceValue];
        });
        article.querySelector(".defects-title").textContent = text.defectsTitle;
        article.querySelectorAll("[data-defect-value]").forEach(label =>
        {
            label.textContent = text.defects[label.dataset.defectValue];
        });
        article.querySelector(".notes-title").textContent = text.notesTitle;
        article.querySelector(".notes").placeholder = text.notesPlaceholder;
    });
    updateProgress();
}

function answeredCriteria(article)
{
    return [...article.querySelectorAll(".criterion")].filter(criterion =>
        criterion.querySelector("input[type=radio]:checked"));
}

function updateProgress()
{
    const articles = [...caseList.querySelectorAll(".test-case")];
    let answered = 0;
    let total = 0;
    articles.forEach(article =>
    {
        const criteria = article.querySelectorAll(".criterion");
        const completed = answeredCriteria(article).length;
        answered += completed;
        total += criteria.length;
        const status = article.querySelector(".case-status");
        const isComplete = completed === criteria.length;
        status.textContent = isComplete ? uiText[activeLocale].complete : `${completed} / ${criteria.length}`;
        status.classList.toggle("complete", isComplete);
    });
    progress.textContent = uiText[activeLocale].progress(answered, total);
}

function criterionResult(criterion)
{
    const selected = criterion.querySelector("input[type=radio]:checked");
    return {
        id: criterion.dataset.criterionId,
        result: selected ? selected.value : "unanswered"
    };
}

function caseResult(article)
{
    return {
        id: article.dataset.caseId,
        criteria: [...article.querySelectorAll(".criterion")].map(criterionResult),
        defects: [...article.querySelectorAll(".defects input:checked")].map(input => input.value),
        notes: article.querySelector(".notes").value.trim()
    };
}

function buildReport()
{
    return {
        reportVersion,
        suiteId: suite.id,
        suiteVersion: suite.version,
        locale: activeLocale,
        capture: suite.capture,
        evaluatedAt: new Date().toISOString(),
        cases: [...caseList.querySelectorAll(".test-case")].map(caseResult)
    };
}

function downloadReport(report)
{
    const blob = new Blob([`${JSON.stringify(report, null, 2)}\n`], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = `${suite.id}-report-${new Date().toISOString().replaceAll(":", "-")}.json`;
    link.click();
    URL.revokeObjectURL(url);
}

async function exportReport()
{
    const report = buildReport();
    exportButton.disabled = true;
    reportStatus.classList.remove("error");
    reportStatus.textContent = uiText[activeLocale].savingReport;

    try
    {
        const response = await fetch("api/report", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(report)
        });
        if (!response.ok)
        {
            throw new Error(`Report request failed: ${response.status}`);
        }
        const result = await response.json();
        reportStatus.textContent = uiText[activeLocale].savedReport(result.path);
    }
    catch (error)
    {
        downloadReport(report);
        reportStatus.classList.add("error");
        reportStatus.textContent = uiText[activeLocale].saveFallback(error.message);
    }
    finally
    {
        exportButton.disabled = false;
    }
}

async function loadSuite()
{
    try
    {
        const response = await fetch(querySuitePath(), { cache: "no-store" });
        if (!response.ok)
        {
            throw new Error(`Suite request failed: ${response.status}`);
        }
        renderSuite(await response.json());
    }
    catch (error)
    {
        errorPanel.hidden = false;
        errorPanel.textContent = uiText[activeLocale].loadError(error.message);
        description.textContent = uiText[activeLocale].unavailable;
    }
}

exportButton.addEventListener("click", exportReport);
languageButton.addEventListener("click", () =>
{
    activeLocale = activeLocale === "en" ? "ja" : "en";
    applyLocale();
});
loadSuite();
