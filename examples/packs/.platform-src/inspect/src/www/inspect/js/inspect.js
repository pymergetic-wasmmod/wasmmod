(async function () {
  const params = new URLSearchParams(location.search);
  let theme = params.get("theme");
  const link = document.getElementById("theme-css");
  function applyTheme(name) {
    theme = name || "metal";
    if (link) {
      link.href = "css/themes/" + theme + ".css";
    }
  }
  if (theme) {
    applyTheme(theme);
  }
  async function load(id, path) {
    const el = document.getElementById(id);
    if (!el) {
      return;
    }
    try {
      const r = await fetch(path);
      const t = await r.text();
      el.textContent = t;
      if (id === "caps") {
        try {
          const j = JSON.parse(t);
          if (!params.get("theme") && j.theme) {
            applyTheme(j.theme);
          }
          document.getElementById("role").textContent =
            "role=" + (j.role || "?") + " theme=" + theme;
        } catch (_) {}
      }
    } catch (e) {
      el.textContent = String(e);
    }
  }
  function renderReg(j) {
    const summary = document.getElementById("reg-summary");
    const tbody = document.getElementById("reg-body");
    const raw = document.getElementById("reg");
    if (raw) {
      raw.textContent = JSON.stringify(j, null, 2);
    }
    if (summary) {
      summary.textContent =
        "methods=" +
        (j.method_count || 0) +
        " gaps=" +
        (j.gap_count || 0);
    }
    if (!tbody) {
      return;
    }
    tbody.textContent = "";
    const methods = j.methods || [];
    for (let i = 0; i < methods.length; i++) {
      const m = methods[i];
      const callees = m.callees || [];
      const callers = m.callers || [];
      const langs = [];
      let honesty = "ok";
      for (let c = 0; c < callees.length; c++) {
        langs.push(callees[c].lang || "?");
        if (callees[c].honesty && callees[c].honesty !== "ok") {
          honesty = callees[c].honesty;
        }
      }
      const tr = document.createElement("tr");
      if (honesty !== "ok" || langs.length < 3) {
        tr.className = "gap";
      }
      const cells = [
        m.module || "",
        m.func || "",
        String(callees.length),
        langs.join(","),
        String(callers.length),
        honesty,
      ];
      for (let k = 0; k < cells.length; k++) {
        const td = document.createElement("td");
        td.textContent = cells[k];
        tr.appendChild(td);
      }
      tbody.appendChild(tr);
    }
  }
  /* Relative to /inspect/ or /cdn/inspect/ → sibling host routes. */
  await load("health", "../health");
  await load("caps", "../capabilities");
  await load("self", "self");
  try {
    const r = await fetch("reg");
    const t = await r.text();
    try {
      renderReg(JSON.parse(t));
    } catch (_) {
      const el = document.getElementById("reg");
      if (el) {
        el.textContent = t;
      }
    }
  } catch (e) {
    const el = document.getElementById("reg");
    if (el) {
      el.textContent = String(e);
    }
  }
})();
