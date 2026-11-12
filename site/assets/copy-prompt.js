// The "Copy Neura Language Skill" control of the front page. It copies the prompt that installs the Neura
// language skill in a coding agent: the code block under the heading "Write Neura with an AI agent", which the
// build includes from README.md. Without clipboard support the button stays hidden and the prompt remains
// selectable. The script requests nothing.
(() => {
  const section = document.querySelector(".agent-skill");
  const block = section && section.querySelector("pre");
  const actions = section && section.querySelector(".skill-actions");
  if (!block || !actions || !navigator.clipboard?.writeText) return;
  const prompt = block.querySelector("code").textContent.replace(/\n$/, "");
  const control = actions.querySelector("[data-copy-prompt]");
  const feedback = actions.querySelector(".skill-copy-feedback");
  const status = actions.querySelector("[data-copy-status]");
  let copying = false;
  let reset;

  block.before(actions);
  actions.hidden = false;
  control.addEventListener("click", async () => {
    if (copying) return;
    copying = true;
    clearTimeout(reset);
    control.dataset.copyState = "pending";
    feedback.textContent = "Copying…";
    status.textContent = "";
    try {
      await navigator.clipboard.writeText(prompt);
      feedback.textContent = "Copied";
      status.textContent = "Neura language skill prompt copied.";
    } catch {
      feedback.textContent = "Select prompt to copy";
      status.textContent = "Could not copy. Select the prompt below and copy it manually.";
    } finally {
      copying = false;
      control.dataset.copyState = "complete";
      reset = setTimeout(() => {
        delete control.dataset.copyState;
        status.textContent = "";
      }, 2400);
    }
  });
})();
