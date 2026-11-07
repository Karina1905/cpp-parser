const $ = (id) => document.getElementById(id);
const code = $("code"), gutter = $("gutter"), status = $("status");

function updateGutter() {
  const n = code.value.split("\n").length;
  if (gutter.childElementCount !== n) {
    gutter.innerHTML = "";
    for (let i = 1; i <= n; i++) { const d = document.createElement("div"); d.textContent = i; gutter.appendChild(d); }
  }
  gutter.scrollTop = code.scrollTop;
}
function markErrorLines(lines) {
  const set = new Set(lines);
  [...gutter.children].forEach((d, i) => d.classList.toggle("err", set.has(i + 1)));
}
code.addEventListener("input", updateGutter);
code.addEventListener("scroll", () => gutter.scrollTop = code.scrollTop);
code.addEventListener("keydown", (e) => {
  if (e.key === "Tab") {
    e.preventDefault();
    const s = code.selectionStart, t = code.selectionEnd;
    code.value = code.value.slice(0, s) + "    " + code.value.slice(t);
    code.selectionStart = code.selectionEnd = s + 4;
    updateGutter();
  }
  if ((e.metaKey || e.ctrlKey) && e.key === "Enter") { e.preventDefault(); runCode(); }
});

document.querySelectorAll(".tab").forEach(tab => tab.addEventListener("click", () => showTab(tab.dataset.tab)));
function showTab(name) {
  document.querySelectorAll(".tab").forEach(t => t.classList.toggle("active", t.dataset.tab === name));
  document.querySelectorAll(".panel").forEach(p => p.classList.toggle("active", p.id === "panel-" + name));
  if (name === "ast" && astRoot) setTimeout(fit, 30);
}

let examples = [];
async function loadExamples() {
  examples = await (await fetch("/api/examples")).json();
  const sel = $("examples");
  sel.innerHTML = examples.map(e => `<option value="${e.name}">${e.name}.cpp</option>`).join("");
  sel.addEventListener("change", () => pickExample(sel.value));
  pickExample(examples.find(e => e.name === "shapes") ? "shapes" : examples[0]?.name);
}
function pickExample(name) {
  const ex = examples.find(e => e.name === name);
  if (!ex) return;
  $("examples").value = name;
  code.value = ex.code;
  $("stdin").value = ex.stdin.trim();
  updateGutter();
  markErrorLines([]);
  runCode();
}

function showOutput(pre, res) {
  if (!res) { pre.textContent = ""; return; }
  pre.textContent = res.out;
  const err = (res.err || "").split("\n").filter(l => l && !l.startsWith("[ok]") && l !== "[run]").join("\n");
  if (err) {
    const s = document.createElement("span"); s.className = "stderr"; s.textContent = err; pre.appendChild(s);
  }
  if (res.timeout) {
    const s = document.createElement("span"); s.className = "stderr"; s.textContent = "\n[зупинено: перевищено 5 с]"; pre.appendChild(s);
  }
}
function exitLabel(el, res) {
  if (!res) { el.textContent = ""; return; }
  el.textContent = res.timeout ? "timeout" : "exit " + res.exit;
  el.classList.toggle("bad", res.exit !== 0);
}

async function runCode() {
  const btn = $("run");
  btn.disabled = true;
  status.className = "status"; status.textContent = "Розбір…";
  try {
    const r = await fetch("/api/run", { method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code: code.value, stdin: $("stdin").value }) });
    render(await r.json());
  } catch (e) {
    status.className = "status bad"; status.textContent = "Сервер не відповідає: " + e.message;
  } finally { btn.disabled = false; }
}
$("run").addEventListener("click", runCode);

function render(res) {
  if (res.error) { status.className = "status bad"; status.textContent = res.error; return; }
  const msgs = (res.messages || "").split("\n").filter(l => l && !l.startsWith("[ok]") && l !== "[run]");
  $("messages").textContent = msgs.join("\n") || "Повідомлень немає";
  const errCount = msgs.filter(l => /^(Semantic|Syntax|Runtime|Codegen) error/.test(l)).length;
  $("msg-count").textContent = errCount;
  $("msg-count").classList.toggle("show", errCount > 0 && !res.sema_ok);

  markErrorLines(res.sema_ok ? [] : [...(res.messages || "").matchAll(/line (\d+)/g)].map(m => +m[1]));

  if (!res.parse_ok || !res.sema_ok) {
    status.className = "status bad";
    status.textContent = !res.parse_ok ? (msgs[0] || "Помилка розбору")
      : `Семантичних помилок: ${errCount}` + (res.ast ? `, AST побудовано (${countNodes(res.ast)} вузлів)` : "");
    showOutput($("interp-out"), null); showOutput($("py-out"), null);
    exitLabel($("interp-exit"), null); exitLabel($("py-exit"), null);
    $("match").className = "match"; $("match").textContent = "";
    $("py-code").textContent = "";
    loadAst(res.ast || null);
    showTab("messages");
    return;
  }

  const n = countNodes(res.ast);
  status.className = "status ok";
  status.textContent = `Синтаксис і семантика без помилок, вузлів AST: ${n}`;

  showOutput($("interp-out"), res.interp); showOutput($("py-out"), res.py);
  exitLabel($("interp-exit"), res.interp); exitLabel($("py-exit"), res.py);
  const same = res.interp && res.py && res.interp.out === res.py.out;
  $("match").className = "match " + (same ? "ok" : "bad");
  $("match").textContent = same ? "Вивід інтерпретатора і Python збігається" : "Вивід відрізняється";

  $("py-code").textContent = res.python || "";
  loadAst(res.ast);
  if (document.querySelector(".tab.active").dataset.tab === "messages") showTab("output");
}
function countNodes(n) { return n ? 1 + (n.children || []).reduce((s, c) => s + countNodes(c), 0) : 0; }

$("copy-py").addEventListener("click", () => navigator.clipboard.writeText($("py-code").textContent));
$("download-py").addEventListener("click", () => {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([$("py-code").textContent], { type: "text/x-python" }));
  a.download = "program.py"; a.click();
});

/* ---------------- AST (d3) ---------------- */
const CATEGORY = {
  decl: new Set(["Program","FuncDecl","MethodDef","Method","MethodProto","FuncProto","ClassDecl","Ctor","Field","VarDecl","ObjDecl","ArrayDecl","Decls","Param","Params","Base","InitList","Init","ClassRef","Dim"]),
  stmt: new Set(["Block","ExprStmt","If","While","DoWhile","For","Return","Break","Continue","Cout","Cin","Empty","Endl"]),
  expr: new Set(["BinOp","UnOp","Assign","IncDec","Ternary","Cast","Call","MethodCall","Member","Index","Ident","This","Args"]),
  lit:  new Set(["IntLit","DoubleLit","StringLit","CharLit","BoolLit"]),
};
const COLORS = { decl: "#3F6ADB", stmt: "#2C9A5F", expr: "#E0862A", lit: "#9A5FB8", other: "#8A939E" };
const cat = (t) => { for (const k in CATEGORY) if (CATEGORY[k].has(t)) return k; return "other"; };
const label = (d) => {
  const t = d.data.type, v = d.data.value;
  if (!v) return [t, ""];
  if (t === "StringLit") return [t, JSON.stringify(v)];
  if (t === "CharLit") return [t, "'" + v + "'"];
  return [t, v];
};

const svg = d3.select("#ast"), g = svg.append("g");
const gLinks = g.append("g"), gNodes = g.append("g");
const tip = $("tip");
const zoom = d3.zoom().scaleExtent([0.05, 4]).on("zoom", e => g.attr("transform", e.transform));
svg.call(zoom);
const tree = d3.tree().nodeSize([26, 190]);
let astRoot = null, idSeq = 0;

function loadAst(data) {
  $("ast-empty").style.display = data ? "none" : "flex";
  gNodes.selectAll("*").remove(); gLinks.selectAll("*").remove();
  if (!data) { astRoot = null; $("ast-stats").textContent = ""; return; }
  astRoot = d3.hierarchy(data);
  astRoot.x0 = 0; astRoot.y0 = 0;
  astRoot.each(d => d.id = ++idSeq);
  collapseToFunctions();
  update(astRoot);
  if ($("panel-ast").classList.contains("active")) setTimeout(fit, 30);
}
function collapseToFunctions() {
  astRoot.descendants().forEach(d => { if (d.children) { d._children = d.children; d.children = null; } });
  astRoot.children = astRoot._children; astRoot._children = null;
  (astRoot.children || []).forEach(c => { if (c.data.type === "ClassDecl") { c.children = c._children; c._children = null; } });
}
function expandAll() { astRoot.descendants().forEach(d => { if (d._children) { d.children = d._children; d._children = null; } }); update(astRoot); }

function update(source) {
  tree(astRoot);
  const nodes = astRoot.descendants(), links = astRoot.links();
  const t = svg.transition().duration(300);

  const node = gNodes.selectAll("g.node").data(nodes, d => d.id);
  const enter = node.enter().append("g").attr("class", "node")
    .attr("transform", `translate(${source.y0},${source.x0})`)
    .on("click", (e, d) => {
      if (d.children) { d._children = d.children; d.children = null; }
      else if (d._children) { d.children = d._children; d._children = null; }
      update(d);
    })
    .on("mousemove", (e, d) => {
      const box = $("ast").getBoundingClientRect();
      tip.style.display = "block";
      tip.style.left = (e.clientX - box.left + 14) + "px";
      tip.style.top = (e.clientY - box.top + 14) + "px";
      tip.textContent = `${d.data.type}${d.data.value ? " = " + d.data.value : ""}\nрядок: ${d.data.line ?? "?"}\nпіддерево: ${countNodes(d.data)}`;
    })
    .on("mouseleave", () => tip.style.display = "none");
  enter.append("circle").attr("r", 1e-6).attr("fill", d => COLORS[cat(d.data.type)]);
  enter.append("text").attr("dy", "-0.25em").attr("x", 10).text(d => label(d)[0]);
  enter.append("text").attr("class", "val").attr("dy", "0.95em").attr("x", 10).text(d => label(d)[1]).attr("fill", d => COLORS[cat(d.data.type)]);

  const all = enter.merge(node);
  all.classed("collapsed", d => !!d._children);
  all.transition(t).attr("transform", d => `translate(${d.y},${d.x})`);
  all.select("circle").transition(t).attr("r", d => d._children ? 7 : 5.5);
  node.exit().transition(t).attr("transform", `translate(${source.y},${source.x})`).remove();

  const diag = d3.linkHorizontal().x(d => d.y).y(d => d.x);
  const link = gLinks.selectAll("path.link").data(links, d => d.target.id);
  link.enter().append("path").attr("class", "link")
    .attr("d", () => { const o = { x: source.x0, y: source.y0 }; return diag({ source: o, target: o }); })
    .merge(link).transition(t).attr("d", diag);
  link.exit().transition(t).attr("d", () => { const o = { x: source.x, y: source.y }; return diag({ source: o, target: o }); }).remove();

  nodes.forEach(d => { d.x0 = d.x; d.y0 = d.y; });
  $("ast-stats").textContent = `показано ${nodes.length} з ${countNodes(astRoot.data)} вузлів`;
}
function fit() {
  if (!astRoot) return;
  const b = g.node().getBBox(), w = svg.node().clientWidth, h = svg.node().clientHeight;
  if (!b.width || !w) return;
  const k = Math.min(0.95 * w / (b.width + 160), 0.95 * h / (b.height + 40), 1.5);
  const tx = w / 2 - k * (b.x + b.width / 2), ty = h / 2 - k * (b.y + b.height / 2);
  svg.transition().duration(300).call(zoom.transform, d3.zoomIdentity.translate(tx, ty).scale(k));
}
$("expand-all").addEventListener("click", () => { expandAll(); setTimeout(fit, 320); });
$("collapse-all").addEventListener("click", () => { collapseToFunctions(); update(astRoot); setTimeout(fit, 320); });
$("fit").addEventListener("click", fit);

updateGutter();
loadExamples();
