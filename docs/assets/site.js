(function () {
  var root = document.documentElement;
  var btn = document.getElementById("theme-toggle");
  if (!btn) return;

  function label(theme) {
    btn.textContent = theme === "dark" ? "Light" : "Dark";
    btn.setAttribute("aria-pressed", theme === "dark" ? "true" : "false");
  }

  function apply(theme) {
    if (theme === "dark") root.setAttribute("data-theme", "dark");
    else root.removeAttribute("data-theme");
    try {
      localStorage.setItem("cforge-theme", theme);
    } catch (e) {}
    label(theme);
  }

  btn.addEventListener("click", function () {
    apply(root.getAttribute("data-theme") === "dark" ? "light" : "dark");
  });

  label(root.getAttribute("data-theme") === "dark" ? "dark" : "light");
})();
