#!/usr/bin/env python3
import json, os, re, subprocess, sys, textwrap
from dataclasses import dataclass
from pathlib import Path
import typer
from rich.console import Console
from rich.markdown import Markdown
from rich.panel import Panel
from rich.syntax import Syntax
from openai import OpenAI
import difflib

app = typer.Typer(help="CLI estilo Claude usando OpenAI (Responses API)")
console = Console()
client = OpenAI(api_key=os.getenv("OPENAI_API_KEY"))

DEFAULT_MODEL = os.getenv("GPTX_MODEL", "gpt-5-mini")  # ajuste aqui


# --------------------------- Util ------------------------------------------
def read_text(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def write_text(p: Path, content: str):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")


def show_diff(old: str, new: str, path: str):
    a = (old or "").splitlines(False)
    b = (new or "").splitlines(False)
    udiff = "\n".join(
        difflib.unified_diff(
            a, b, fromfile=f"{path}:old", tofile=f"{path}:new", lineterm=""
        )
    )
    if udiff.strip():
        console.print(Syntax(udiff + "\n", "diff"))
    else:
        console.print(f"[green]Sem alterações em {path}[/green]")


# ---------------------- Structured Outputs schema --------------------------
EDIT_SCHEMA = {
    "name": "EditPlan",
    "schema": {
        "type": "object",
        "additionalProperties": False,
        "properties": {
            "changes": {
                "type": "array",
                "items": {
                    "type": "object",
                    "additionalProperties": False,
                    "properties": {
                        "path": {
                            "type": "string",
                            "description": "Caminho do arquivo (relativo).",
                        },
                        "action": {
                            "type": "string",
                            "enum": [
                                "replace_regex",
                                "replace_region",
                                "insert_before",
                                "insert_after",
                                "create_file",
                                "delete_lines",
                            ],
                        },
                        "pattern": {
                            "type": "string",
                            "description": "Regex para localizar trecho (quando aplicável).",
                        },
                        "start_marker": {"type": "string"},
                        "end_marker": {"type": "string"},
                        "start_line": {"type": "integer", "minimum": 1},
                        "end_line": {"type": "integer", "minimum": 1},
                        "occurrence": {
                            "type": "integer",
                            "minimum": 1,
                            "description": "Qual ocorrência da pattern alterar (default=1).",
                        },
                        "text": {
                            "type": "string",
                            "description": "Texto a inserir/substituir.",
                        },
                    },
                    "required": ["path", "action"],
                },
            },
            "message": {"type": "string"},
        },
        "required": ["changes"],
    },
    "strict": True,
}


# --------------------------- Chat ------------------------------------------
@app.command()
def chat(
    prompt: str = typer.Argument(..., help="Pergunta ou instrução."),
    model: str = typer.Option(DEFAULT_MODEL, "--model", "-m"),
    stream: bool = typer.Option(True, help="Streaming de texto."),
):
    """Chat simples com streaming."""
    resp = client.responses.create(model=model, input=prompt)
    console.print(Markdown(resp.output_text))


# ----------------------- Edição Pontual de Arquivo -------------------------
def apply_change(base_dir: Path, change: dict):
    path = base_dir / change["path"]
    action = change["action"]
    if action == "create_file":
        write_text(path, change.get("text", ""))
        return (None, change.get("text", ""))
    if not path.exists():
        raise FileNotFoundError(f"Arquivo não encontrado: {path}")
    original = read_text(path)
    new = original

    if action == "replace_regex":
        pat = re.compile(change["pattern"], re.DOTALL | re.MULTILINE)
        occ = change.get("occurrence", 1)

        def repl_nth(match_iter, n):
            buf, i = [], 0
            last_end = 0
            for m in match_iter:
                i += 1
                if i == n:
                    buf.append(original[last_end : m.start()])
                    buf.append(change.get("text", ""))
                    last_end = m.end()
                    break
            buf.append(original[last_end:])
            return "".join(buf)

        new = repl_nth(pat.finditer(original), occ)

    elif action == "replace_region":
        # delimitado por start_marker e end_marker OU por start_line/end_line
        if change.get("start_marker") and change.get("end_marker"):
            s = re.search(change["start_marker"], original, re.DOTALL | re.MULTILINE)
            e = re.search(change["end_marker"], original, re.DOTALL | re.MULTILINE)
            if not s or not e or e.start() < s.end():
                raise ValueError("Região não encontrada/ordem inválida")
            new = original[: s.end()] + change.get("text", "") + original[e.start() :]
        else:
            sl = change.get("start_line")
            el = change.get("end_line")
            lines = original.splitlines(keepends=True)
            new = "".join(lines[: sl - 1] + [change.get("text", "")] + lines[el:])

    elif action in ("insert_before", "insert_after"):
        anchor = re.search(change["pattern"], original, re.DOTALL | re.MULTILINE)
        if not anchor:
            raise ValueError("Âncora não encontrada")
        if action == "insert_before":
            new = (
                original[: anchor.start()]
                + change.get("text", "")
                + original[anchor.start() :]
            )
        else:
            new = (
                original[: anchor.end()]
                + change.get("text", "")
                + original[anchor.end() :]
            )

    elif action == "delete_lines":
        sl = change["start_line"]
        el = change["end_line"]
        lines = original.splitlines(keepends=True)
        new = "".join(lines[: sl - 1] + lines[el:])

    else:
        raise ValueError(f"Ação desconhecida: {action}")

    write_text(path, new)
    return (original, new)


@app.command()
def edit(
    instruction: str = typer.Argument(
        ..., help="ex: 'renomeie a função foo para bar e atualize os usos'"
    ),
    paths: list[Path] = typer.Option(
        [], "--path", "-p", help="Arquivos (pode repetir)"
    ),
    root: Path = typer.Option(Path("."), "--root", help="Raiz do projeto"),
    model: str = typer.Option(DEFAULT_MODEL, "--model", "-m"),
    dry_run: bool = typer.Option(False, "--dry-run", help="Mostra diffs sem gravar"),
):
    """
    Gera um plano de edição (Structured Outputs) e aplica com alta precisão.
    """
    context_blobs = []
    for p in paths:
        if p.exists() and p.is_file():
            txt = read_text(p)
            context_blobs.append(f"--- file:{p}\n{txt}\n")

    sys_prompt = textwrap.dedent(
        f"""
    Você é um refatorador de código. Gere um plano JSON que faça alterações pontuais e mínimas,
    evitando alterações desnecessárias e preservando formatação quando possível.
    Use apenas ações do schema.
    """
    )

    resp = client.responses.create(
        model=model,
        input=[
            {"role": "developer", "content": sys_prompt},
            {
                "role": "user",
                "content": instruction
                + ("\n\n" + "\n".join(context_blobs) if context_blobs else ""),
            },
        ],
        response_format={"type": "json_schema", "json_schema": EDIT_SCHEMA},
    )

    plan = json.loads(resp.output_text)
    console.print(Panel.fit("Plano de edição gerado"))
    console.print_json(data=plan)

    # aplicar
    for ch in plan["changes"]:
        original, new = None, None
        if not dry_run:
            original, new = apply_change(root, ch)
        else:
            # simula
            path = root / ch["path"]
            if path.exists():
                original = read_text(path)
                # simula passando por apply_change sem gravar
                try:
                    _old, new = apply_change(root, {**ch})
                    # reverte gravação imediata (dry-run)
                    if path.exists() and _old is not None:
                        write_text(path, _old)
                except Exception as e:
                    console.print(f"[red]Falha ao simular mudança em {ch['path']}: {e}")
                    continue
        if original is not None and new is not None:
            show_diff(original, new, ch["path"])


# ----------------------------- Modo Agente ---------------------------------
AGENT_TOOLS = [
    {
        "type": "function",
        "name": "read_file",
        "description": "Ler arquivo de texto UTF-8",
        "parameters": {
            "type": "object",
            "properties": {"path": {"type": "string"}},
            "required": ["path"],
        },
    },
    {
        "type": "function",
        "name": "write_file",
        "description": "Gravar arquivo de texto UTF-8 (cria diretórios).",
        "parameters": {
            "type": "object",
            "properties": {"path": {"type": "string"}, "content": {"type": "string"}},
            "required": ["path", "content"],
        },
    },
    {
        "type": "function",
        "name": "run_sh",
        "description": "Executar comando shell e retornar stdout/stderr. Use com parcimônia.",
        "parameters": {
            "type": "object",
            "properties": {
                "cmd": {"type": "string"},
                "cwd": {"type": "string", "default": "."},
            },
            "required": ["cmd"],
        },
    },
]


def call_tool(name: str, args: dict):
    print(f"[ferramenta] Chamando {name} com args {args}")
    if name == "read_file":
        p = Path(args["path"])
        return {"ok": True, "content": read_text(p) if p.exists() else ""}
    if name == "write_file":
        p = Path(args["path"])
        write_text(p, args["content"])
        return {"ok": True}
    if name == "run_sh":
        if input(
            f"Permitir exec: `{args['cmd']}` em {args.get('cwd','.') } ?"
        ) .lower() not in ("y", "yes"):
            return {"ok": False, "error": "cancelado pelo usuário"}
        proc = subprocess.run(
            args["cmd"],
            shell=True,
            cwd=args.get("cwd", "."),
            capture_output=True,
            text=True,
        )
        return {
            "ok": proc.returncode == 0,
            "code": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
        }
    return {"ok": False, "error": f"tool {name} desconhecida"}


# ======================== REPL híbrido (chat + agente) ========================

def run_agent_round(
    client: OpenAI,
    model: str,
    messages: list[dict],
    tools: list[dict],
    max_steps: int = 8,
) -> tuple[list[dict], str]:
    """Executa 1 rodada em modo agente, incluindo ciclos de tool calls.
    Retorna (messages_atualizados, texto_final_exibido)."""
    # primeira chamada (o modelo decide se chama funções)
    resp = client.responses.create(
        model=model,
        input=messages,
        tools=tools,
        parallel_tool_calls=True,
    )

    final_text_parts: list[str] = []
    if resp.output_text:
        final_text_parts.append(resp.output_text)

    # loop de tool-use -> enviar function_call_output -> nova rodada
    for _ in range(max_steps):
        print(f"[agente] rodada de tool calls...")
        calls = [it for it in (resp.output or []) if getattr(it, "type", "") == "function_call"]
        if not calls:
            break

        out_items = []
        for call in calls:
            name = getattr(call, "name", "")
            args_json = getattr(call, "arguments", "{}") or "{}"
            try:
                args = json.loads(args_json)
            except Exception:
                args = {}

            # executa tool local
            result = call_tool(name, args)

            # Responses API exige 'output' como string (ou lista de "content parts")
            try:
                out_str = result if isinstance(result, str) else json.dumps(result, ensure_ascii=False)
            except Exception:
                out_str = str(result)

            print(f"[agente] tool {name} retornou: {out_str}")

            out_items.append({
                "type": "function_call_output",
                "call_id": call.call_id,
                "output": out_str,
            })

        # encadeia o estado desta conversa
        resp = client.responses.create(
            model=model,
            previous_response_id=resp.id,
            input=out_items,
        )

        if resp.output_text:
            final_text_parts.append(resp.output_text)

    # anexa um “assistant turn” ao histórico para manter o contexto no REPL
    full_text = "\n".join([t for t in final_text_parts if t])
    if full_text.strip():
        messages.append({"role": "assistant", "content": full_text})
    return messages, full_text


def run_chat_round(
    client: OpenAI,
    model: str,
    messages: list[dict],
) -> tuple[list[dict], str]:
    """Rodada simples (sem ferramentas)."""
    resp = client.responses.create(model=model, input=messages)
    text = resp.output_text or ""
    if text.strip():
        messages.append({"role": "assistant", "content": text})
    return messages, text


@app.command()
def repl(
    model: str = typer.Option(DEFAULT_MODEL, "--model", "-m"),
    max_steps: int = typer.Option(8, help="Máximo de iterações por rodada em modo agente"),
):
    """
    REPL híbrido: converse no modo chat e ligue/desligue o modo agente com comandos.
    Comando ad-hoc: prefixe sua mensagem com '!' para forçar agente apenas nessa rodada.
    """
    # estado do REPL
    agent_mode = False
    messages: list[dict] = [
        {"role": "developer", "content": (
            "Você é um assistente de terminal. Responda em português, de forma sucinta.\n"
            "Quando o modo agente estiver ativo, você poderá usar ferramentas registradas."
        )}
    ]

    # tenta usar prompt_toolkit; se não tiver, fallback pra input()
    try:
        from prompt_toolkit import PromptSession
        from prompt_toolkit.history import FileHistory
        from prompt_toolkit.completion import WordCompleter
        from prompt_toolkit.key_binding import KeyBindings
        from prompt_toolkit.patch_stdout import patch_stdout

        history_path = Path.home() / ".gptx_history"
        completer = WordCompleter(
            [
                ":help", ":agent on", ":agent off", ":agent toggle",
                ":model", ":reset", ":read", ":quit", ":q",
            ],
            ignore_case=True,
        )

        # Enter = enviar; Shift+Enter = nova linha
        kb = KeyBindings()

        @kb.add("s-enter")  # Shift+Enter
        def _(event):
            event.current_buffer.insert_text("\n")

        session = PromptSession(
            message=lambda: f"[{'AGENT' if agent_mode else 'CHAT'} {model}] › ",
            history=FileHistory(str(history_path)),
            completer=completer,
            multiline=False,      # <<< importante: agora Enter envia
            key_bindings=kb,      # <<< habilita Shift+Enter = newline
        )

        def _readline(prompt: str) -> str:
            with patch_stdout():
                return session.prompt()
    except Exception:
        # fallback simples
        def _readline(prompt: str) -> str:
            return input(prompt)

    console.print(Panel.fit(
        "REPL iniciado. Digite ':help' para comandos. "
        "Prefixe com '!' para rodar apenas essa mensagem em modo agente.",
        title="gptx REPL"
    ))

    while True:
        try:
            line = _readline(f"[{'AGENT' if agent_mode else 'CHAT'} {model}] › ")
        except (KeyboardInterrupt, EOFError):
            console.print("\n[bold]Até mais![/bold]")
            break

        if not line.strip():
            continue

        # ---------------- Comandos de controle ----------------
        if line.strip().startswith(":"):
            parts = line.strip().split()
            cmd = parts[0].lower()

            if cmd in (":quit", ":q"):
                break

            elif cmd == ":help":
                console.print(Markdown(
                    """
**Comandos**
- `:agent on|off|toggle` — liga/desliga o modo agente.
- `:model NOME` — troca o modelo (ex: `:model gpt-4o-mini`).
- `:reset` — limpa o contexto (mantém o modo e o modelo).
- `:read CAMINHO` — injeta conteúdo de arquivo como mensagem do usuário.
- `:quit` / `:q` — sair.

**Dicas**
- Prefixe sua mensagem com `!` para forçar **apenas essa rodada** em modo agente.
- Use multi-linha (Shift+Enter) no prompt para escrever mensagens longas.
                    """
                ))
                continue

            elif cmd == ":agent":
                arg = (parts[1].lower() if len(parts) > 1 else "toggle")
                if arg in ("on", "true", "1"):
                    agent_mode = True
                elif arg in ("off", "false", "0"):
                    agent_mode = False
                else:
                    agent_mode = not agent_mode
                console.print(f"[bold]Modo agente[/bold]: {'ligado' if agent_mode else 'desligado'}")
                continue

            elif cmd == ":model":
                if len(parts) < 2:
                    console.print("[red]Uso: :model NOME_DO_MODELO[/red]")
                else:
                    model = parts[1]
                    console.print(f"Modelo trocado para [bold]{model}[/bold]")
                continue

            elif cmd == ":reset":
                messages = messages[:1]  # mantém só a primeira mensagem 'developer'
                console.print("[yellow]Contexto limpo.[/yellow]")
                continue

            elif cmd == ":read":
                if len(parts) < 2:
                    console.print("[red]Uso: :read CAMINHO[/red]")
                else:
                    path = Path(parts[1])
                    try:
                        content = read_text(path)
                        messages.append({"role": "user", "content": f"--- file:{path}\n{content}\n"})
                        console.print(f"[green]Arquivo {path} adicionado ao contexto.[/green]")
                    except Exception as e:
                        console.print(f"[red]Falha ao ler {path}: {e}[/red]")
                continue

            else:
                console.print(f"[red]Comando desconhecido:[/red] {cmd}")
                continue

        # ---------------- Mensagem normal ----------------
        # ad-hoc: '!' força agente só nesta mensagem
        force_agent = False
        text = line
        if line.startswith("!"):
            force_agent = True
            text = line[1:].lstrip()

        messages.append({"role": "user", "content": text})

        # decide o modo desta rodada
        use_agent = agent_mode or force_agent

        # roda a rodada
        from rich.status import Status
        with console.status("Consultando modelo...", spinner="dots"):
            if use_agent:
                messages, text_out = run_agent_round(
                    client=client,
                    model=model,
                    messages=messages,
                    tools=AGENT_TOOLS,
                    max_steps=max_steps,
                )
            else:
                messages, text_out = run_chat_round(
                    client=client,
                    model=model,
                    messages=messages,
                )

        if text_out.strip():
            console.print(Markdown(text_out))
        else:
            console.print("[dim]◦ (sem saída de texto)[/dim]")



@app.command()
def agent(
    goal: str = typer.Argument(
        ..., help="Objetivo, ex: 'varrer src/ e extrair todos os TODOs em TODO.md'"
    ),
    model: str = typer.Option("gpt-4o-mini", "--model", "-m"),
    web: bool = typer.Option(False, "--web", help="(reservado)"),
    max_steps: int = typer.Option(8, help="Máximo de iterações do agente"),
):
    """
    Agente simples: o modelo decide ferramentas; nós executamos e devolvemos os outputs.
    """
    tools = AGENT_TOOLS.copy()
    # (se um dia quiser hospedar web_search, acrescenta aqui)

    dev = (
        "Você é um agente de terminal, minimalista e cuidadoso. "
        "Use SOMENTE as ferramentas registradas (read_file, write_file, run_sh). "
        "Antes de rodar shell, o host pedirá confirmação. "
        "Quando precisar ler arquivos, chame read_file; para escrever, write_file; "
        "para listar/grep/etc, use run_sh com comandos simples e seguros. "
        "Responda em português e explique sucintamente o que fez."
    )

    # 1ª chamada: objetivo + ferramentas
    context = [
        {"role": "developer", "content": dev},
        {"role": "user", "content": goal},
    ]

    resp = client.responses.create(
        model=model,
        input=context,
        tools=tools,
        parallel_tool_calls=True,
    )

    # imprime texto, se houver
    if resp.output_text:
        console.print(Markdown(resp.output_text))

    # loop de tool-use -> devolve function_call_output -> nova rodada
    for _ in range(max_steps):
        # colete TODAS as chamadas de função desta rodada
        calls = [
            it
            for it in (resp.output or [])
            if getattr(it, "type", "") == "function_call"
        ]
        if not calls:
            break  # nada para executar -> terminou

        # execute cada call localmente
        out_items = []
        for call in calls:
            name = getattr(call, "name", "")
            args_json = getattr(call, "arguments", "{}") or "{}"
            try:
                args = json.loads(args_json)
            except Exception:
                args = {}

            result = call_tool(name, args)

            # *** IMPORTANTE: Responses API espera output como STRING ***
            try:
                out_str = (
                    result
                    if isinstance(result, str)
                    else json.dumps(result, ensure_ascii=False)
                )
            except Exception:
                out_str = str(result)

            out_items.append(
                {
                    "type": "function_call_output",
                    "call_id": call.call_id,  # mapeia exatamente para a call original
                    "output": out_str,
                }
            )

        # encadeia a conversa usando previous_response_id
        resp = client.responses.create(
            model=model,
            previous_response_id=resp.id,
            input=out_items,
        )

        # imprime eventual texto da nova rodada
        if resp.output_text:
            console.print(Markdown(resp.output_text))

    # fim do loop (ou chegou no max_steps)


if __name__ == "__main__":
    app()
