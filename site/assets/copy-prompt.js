// The "Copy Neura Language Skill" controls of the front page. Each copies the prompt that installs the Neura
// language skill in a coding agent: the code block under the heading "Write Neura with an AI agent", which the
// build includes from README.md. The page works without this script: the hero's control is then a link to that
// heading, and the button below the prompt stays hidden. The script requests nothing.
(() => {
  const heading = document.getElementById("write-neura-with-an-ai-agent");
  const block = heading && heading.closest(".agent-skill") && heading.closest(".agent-skill").querySelector("pre code");
  if (!block || !navigator.clipboard) return;
  const prompt = block.textContent.replace(/\n$/, "");

  document.querySelectorAll("[data-copy-prompt]").forEach((control) => {
    const label = control.textContent;
    control.hidden = false;
    control.addEventListener("click", (event) => {
      event.preventDefault();
      navigator.clipboard.writeText(prompt).then(
        () => {
          control.textContent = "Copied";
          setTimeout(() => (control.textContent = label), 1600);
        },
        () => heading.scrollIntoView(),
      );
    });
  });
})();
