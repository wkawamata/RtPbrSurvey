"use strict";

const reportVersion = 1;
const defectOptions = [
    ["noise", "Residual noise"],
    ["silhouette-trail", "Silhouette trail"],
    ["internal-trail", "Internal-detail trail"],
    ["flicker", "Flicker"],
    ["screen-edge", "Screen-edge artifact"],
    ["brightness-detail-loss", "Brightness/detail loss"]
];

const caseList = document.querySelector("#case-list");
const caseTemplate = document.querySelector("#case-template");
const description = document.querySelector("#suite-description");
const metadata = document.querySelector("#suite-metadata");
const progress = document.querySelector("#progress");
const exportButton = document.querySelector("#export-report");
const errorPanel = document.querySelector("#suite-error");

let suite = null;

function querySuitePath()
{
    const parameters = new URLSearchParams(window.location.search);
    return parameters.get("suite") || "suite.json";
}

function makeRadioOption(caseId, criterionId, value, label)
{
    const option = document.createElement("label");
    const input = document.createElement("input");
    input.type = "radio";
    input.name = `${caseId}.${criterionId}`;
    input.value = value;
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
        makeRadioOption(testCase.id, criterion.id, "pass", "Pass"),
        makeRadioOption(testCase.id, criterion.id, "fail", "Fail"),
        makeRadioOption(testCase.id, criterion.id, "unable", "Unable to judge")
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
    defectOptions.forEach(([value, label]) =>
    {
        const option = document.createElement("label");
        const input = document.createElement("input");
        input.type = "checkbox";
        input.value = value;
        option.append(input, label);
        defectContainer.append(option);
    });

    caseList.append(fragment);
}

function renderSuite(loadedSuite)
{
    suite = loadedSuite;
    document.title = `${suite.title} - Subjective Validation`;
    description.textContent = suite.description;
    metadata.innerHTML = "";
    Object.entries(suite.capture).forEach(([key, value]) =>
    {
        const item = document.createElement("span");
        const heading = document.createElement("strong");
        heading.textContent = `${key}: `;
        item.append(heading, String(value));
        metadata.append(item);
    });
    suite.cases.forEach(renderCase);
    exportButton.disabled = false;
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
        status.textContent = isComplete ? "Complete" : `${completed} / ${criteria.length}`;
        status.classList.toggle("complete", isComplete);
    });
    progress.textContent = `${answered} / ${total} answered`;
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

function exportReport()
{
    const report = {
        reportVersion,
        suiteId: suite.id,
        suiteVersion: suite.version,
        capture: suite.capture,
        evaluatedAt: new Date().toISOString(),
        cases: [...caseList.querySelectorAll(".test-case")].map(caseResult)
    };
    const blob = new Blob([`${JSON.stringify(report, null, 2)}\n`], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = `${suite.id}-report-${new Date().toISOString().replaceAll(":", "-")}.json`;
    link.click();
    URL.revokeObjectURL(url);
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
        errorPanel.textContent = `Unable to load the validation suite. Serve this directory over HTTP and verify the suite path. ${error.message}`;
        description.textContent = "Validation suite unavailable.";
    }
}

exportButton.addEventListener("click", exportReport);
loadSuite();
