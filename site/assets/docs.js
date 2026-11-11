// The documentation's search box, the highlight of the current section in "On this page",
// and a copy button on each code block. Every page works without this script: the
// navigation and the documentation's home page list every document. docs/Site.md
// describes the site; assets/search.json is written by scripts/site_docs.py.
"use strict";

(function () {
  const KINDS = ["Page", "Section", "Member"];
  let index = null;
  let loading = null;

  function load(root, address) {
    if (index) return Promise.resolve(index);
    if (!loading) {
      loading = fetch(address || root + "assets/search.json")
        .then((response) => response.json())
        .then((data) => {
          const items = [];
          data.pages.forEach((page, number) => {
            items.push({ label: page[0], url: page[1], context: page[2], kind: 0, page: number });
          });
          data.entries.forEach((entry) => {
            const page = data.pages[entry[1]];
            items.push({ label: entry[0], url: page[1] + "#" + entry[2], context: page[0], kind: entry[3] });
          });
          items.forEach((item) => {
            item.lower = item.label.toLowerCase();
            item.haystack = (item.label + " " + item.context).toLowerCase();
          });
          index = items;
          return items;
        });
    }
    return loading;
  }

  function score(item, query, words) {
    let value = 0;
    if (item.lower === query) value = 100;
    else if (item.lower.startsWith(query)) value = 80;
    else if (item.lower.split(/[\s.`_-]+/).some((word) => word.startsWith(query))) value = 60;
    else if (item.lower.includes(query)) value = 40;
    else if (words.every((word) => item.haystack.includes(word))) value = 20;
    else return 0;
    return value + [16, 6, 0][item.kind];
  }

  function search(items, text) {
    const query = text.trim().toLowerCase();
    if (!query) return [];
    const words = query.split(/\s+/);
    const found = [];
    for (const item of items) {
      const value = score(item, query, words);
      if (value) found.push([value, item]);
    }
    found.sort((a, b) => b[0] - a[0] || a[1].label.length - b[1].label.length);
    return found.slice(0, 24).map((pair) => pair[1]);
  }

  function setUp(box) {
    const root = box.dataset.root || "";
    // The build gives the index's address a version, so a new deployment is never searched with an old index.
    const address = box.dataset.index;
    const input = box.querySelector("input");
    const list = box.querySelector(".results");
    let results = [];
    let active = -1;
    box.hidden = false;

    function close() {
      list.hidden = true;
      input.setAttribute("aria-expanded", "false");
      input.removeAttribute("aria-activedescendant");
      active = -1;
    }

    function choose(number) {
      const options = list.querySelectorAll("[role=option]");
      options.forEach((option, position) => option.setAttribute("aria-selected", String(position === number)));
      active = number;
      if (options[number]) {
        input.setAttribute("aria-activedescendant", options[number].id);
        options[number].scrollIntoView({ block: "nearest" });
      }
    }

    function show() {
      load(root, address).then((items) => {
        results = search(items, input.value);
        list.replaceChildren();
        if (!input.value.trim()) {
          close();
          return;
        }
        if (!results.length) {
          const empty = document.createElement("li");
          empty.className = "empty";
          empty.textContent = "No document, section, or member matches.";
          list.append(empty);
        }
        results.forEach((item, number) => {
          const option = document.createElement("li");
          option.id = list.id + "-" + number;
          option.setAttribute("role", "option");
          const link = document.createElement("a");
          link.href = root + item.url;
          link.tabIndex = -1;
          const label = document.createElement("span");
          label.className = "result-label";
          label.textContent = item.label;
          const meta = document.createElement("span");
          meta.className = "result-meta";
          meta.textContent = KINDS[item.kind] + (item.kind ? " in " : " · ") + item.context;
          link.append(label, meta);
          option.append(link);
          list.append(option);
        });
        list.hidden = false;
        input.setAttribute("aria-expanded", "true");
        choose(results.length ? 0 : -1);
      });
    }

    input.addEventListener("focus", () => load(root, address));
    input.addEventListener("input", show);
    input.addEventListener("keydown", (event) => {
      if (event.key === "ArrowDown" && results.length) {
        event.preventDefault();
        choose((active + 1) % results.length);
      } else if (event.key === "ArrowUp" && results.length) {
        event.preventDefault();
        choose((active - 1 + results.length) % results.length);
      } else if (event.key === "Enter" && active >= 0 && results[active]) {
        event.preventDefault();
        window.location.href = root + results[active].url;
      } else if (event.key === "Escape") {
        if (!list.hidden) close();
        else input.blur();
      }
    });
    document.addEventListener("click", (event) => {
      if (!box.contains(event.target)) close();
    });
  }

  function focusSearch(event) {
    const target = event.target;
    const typing = target.closest && target.closest("input, textarea, select, [contenteditable]");
    const slash = event.key === "/" && !typing;
    const command = (event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "k";
    if (!slash && !command) return;
    const boxes = Array.from(document.querySelectorAll("[data-search]:not([hidden])"));
    const visible = boxes.find((box) => box.offsetParent !== null) || boxes[0];
    if (!visible) return;
    event.preventDefault();
    visible.querySelector("input").focus();
  }

  function followContents() {
    const toc = document.querySelector(".docs-toc");
    if (!toc || !("IntersectionObserver" in window)) return;
    const links = new Map();
    toc.querySelectorAll("a[href^='#']").forEach((link) => links.set(decodeURIComponent(link.hash.slice(1)), link));
    const headings = Array.from(links.keys()).map((id) => document.getElementById(id)).filter(Boolean);
    if (!headings.length) return;
    const visible = new Set();
    const observer = new IntersectionObserver((entries) => {
      entries.forEach((entry) => (entry.isIntersecting ? visible.add(entry.target) : visible.delete(entry.target)));
      let current = headings.find((heading) => visible.has(heading));
      if (!current) {
        current = headings.filter((heading) => heading.getBoundingClientRect().top < 120).pop() || headings[0];
      }
      links.forEach((link) => link.classList.remove("current"));
      const link = links.get(current.id);
      if (link) link.classList.add("current");
    }, { rootMargin: "-72px 0px -60% 0px" });
    headings.forEach((heading) => observer.observe(heading));
  }

  function copyButtons() {
    if (!navigator.clipboard) return;
    document.querySelectorAll(".prose pre:not(.numbered)").forEach((block) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "copy";
      button.textContent = "Copy";
      button.addEventListener("click", () => {
        navigator.clipboard.writeText(block.querySelector("code").innerText.replace(/\n$/, "")).then(() => {
          button.textContent = "Copied";
          setTimeout(() => (button.textContent = "Copy"), 1600);
        });
      });
      // The block scrolls sideways when its lines are long; a wrapper that does not scroll holds the button, so the
      // button stays in the corner however far the code is scrolled.
      const frame = document.createElement("div");
      frame.className = "code-frame";
      block.replaceWith(frame);
      frame.append(block, button);
    });
  }

  function revealCurrent() {
    const current = document.querySelector(".docs-nav [aria-current=page]");
    const list = document.querySelector(".docs-nav");
    if (current && list && list.scrollHeight > list.clientHeight) {
      list.scrollTop = current.offsetTop - list.offsetTop - list.clientHeight / 3;
    }
  }

  document.querySelectorAll("[data-search]").forEach(setUp);
  document.addEventListener("keydown", focusSearch);
  followContents();
  copyButtons();
  revealCurrent();
})();
