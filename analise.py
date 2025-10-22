import anthropic
import os
import time
from pathlib import Path
from typing import List, Dict, Optional
import json


class BidiXCodeReviewer:
    def __init__(self, api_key: str, project_path: str):
        """
        Code reviewer especializado para o projeto bidi-x.

        Args:
            api_key: Chave da API do Anthropic
            project_path: Caminho para o projeto bidi-x
        """
        self.client = anthropic.Anthropic(api_key=api_key)
        self.project_path = Path(project_path)
        self.review_history = []
        self.project_context = self._load_project_context()

    def _load_project_context(self) -> str:
        """Carrega toda a documentacao do projeto da pasta .github"""
        context = "# CONTEXTO DO PROJETO BIDI-X\n\n"

        github_dir = self.project_path / ".github"
        if not github_dir.exists():
            print("Aviso: pasta .github nao encontrada")
            return context

        doc_files = [
            "BiDiX.md",
            "Guia.md",
            "Instructs.md",
            "AnaliseBiDiX.md",
            "Spec.md",
            "C_CXX.instructions.md",
            "copilot-instructions.md",
        ]

        for doc_file in doc_files:
            doc_path = github_dir / doc_file
            if doc_path.exists():
                try:
                    content = doc_path.read_text(encoding="utf-8")
                    context += f"\n## {doc_file}\n{content}\n"
                except Exception as e:
                    print(f"Erro ao ler {doc_file}: {e}")

        map_md_file = self.project_path / "map.md"
        if map_md_file.exists():
            try:
                content = map_md_file.read_text(encoding="utf-8")
                context += f"\n## map.md\n{content}\n"
            except Exception as e:
                print(f"Erro ao ler map.md: {e}")

        return context

    def _get_review_rules(self) -> str:
        """Regras especificas de review para bidi-x"""
        return """
# REGRAS DE CODE REVIEW PARA BIDI-X

## ARQUITETURA & DESIGN
1. Verificar adherencia ao modelo assincrono canonico (Boost.Asio/Beast)
2. Validar uso de strand para serializacao de operacoes WS
3. Checar que async_read e async_write nunca sao concorrentes
4. Confirmar lazy evaluation (nada executa sem terminal: .finally, co_await)
5. Validar timeout como corrida (operacao vs steady_timer)
6. Verificar RAII em subscricoes (session.subscribe/unsubscribe com refcount)

## MEMORIA & PERFORMANCE
7. Confirmar uso de PMR/arena por mensagem (boost::json::monotonic_resource)
8. Validar serializacao sem DOM (boost::json::serializer, NAO stringify manual)
9. Checar fast-path no roteamento (scan id/method sem DOM completo)
10. Verificar que string_view nao escapa do lifetime da arena
11. Validar reutilizacao de buffers (freelist, flat_buffer com limite)
12. Checar backpressure (high-water mark na fila TX)

## THREADING & CONCORRENCIA
13. Proibir std::thread().detach() - SEMPRE usar strand ou thread_pool
14. Validar que operacoes bloqueantes vao para thread_pool e postam de volta
15. Verificar cancel cooperativo (steady_timer.cancel, stop_token)
16. Checar que timers nao causam races (generation counter se reutilizados)

## PROTOCOLO BIDI
17. Validar IDs seguros (nao exceder 2^53-1 no wire, usar id_type internamente)
18. Checar shape de mensagens {id,method,params} / {id,result|error}
19. Verificar mapeamento de erros (timeout/transport/server_error/decode/unsupported)
20. Validar preservacao de 'raw' em ErrorDetail para diagnostics
21. Confirmar nomes canonicos 1:1 com spec W3C (sem strings soltas)

## ESPERA SEM POLLING
22. PROIBIR busy loops ou polling - usar timers/handlers/awaitable
23. Para DOM waits: injetar Promise com MutationObserver + timeout
24. Validar mensagens de erro descritivas (nao apenas "timeout")

## C++ CORE GUIDELINES & NASA P10
25. Verificar RAII (unique_ptr por padrao, shared_ptr justificado)
26. Validar const-correctness e [[nodiscard]]
27. Checar inicializacao uniforme {} para todos os membros
28. Validar std::expected<T,E> para erros recuperaveis
29. Proibir goto, recursao nao limitada, loops sem bounds
30. Checar funcoes curtas (<=80 linhas), coesas, uma responsabilidade
31. Validar que nao ha UB, data races, ou alocacao em hot paths

## ESTILO & QUALIDADE
32. Verificar snake_case (funcoes/locals), PascalCase (tipos)
33. Validar early returns, sem ifs aninhados desnecessarios
34. Checar {} mesmo para statements simples if (a) { b(); }
35. Validar ausencia de emojis/icones em codigo/logs/commits
36. Confirmar comentarios explicativos para decisoes nao obvias
37. Verificar que identificadores nao sao redeclarados no mesmo escopo

## TESTES & VALIDACAO
38. Checar cobertura de testes (unit + integration)
39. Validar testes deterministicos (sem clock/network real)
40. Verificar harness de races (respostas fora de ordem, tardias, drop)

## VIOLACOES CRITICAS (FAIL AUTOMATICO)
- Polling ou busy-wait loops
- std::thread().detach()
- async_write concorrentes sem fila no strand
- Stringify manual de JSON (escapar incorreto)
- IDs > 2^53-1 sem estrategia documentada
- string_view apos lifetime da arena
- Redeclaracao de identificadores no mesmo escopo
- Ausencia de {} em statements simples

OBS diversas:
- Alguns arquivos podem ser implementações de baixo nível, outros são fluxos de alto nível, garanta o uso dos primitivos corretos.
- Considere o contexto do projeto e as instruções ao revisar cada arquivo.
- Se o arquivo for muito grande, foque na estrutura geral e nos pontos críticos em primeiro lugar, sem se perder em detalhes.
- Se o arquivo for um teste, verifique se cobre os casos principais e edge cases.
"""

    def _get_cpp_files(self) -> List[Path]:
        """Encontra arquivos C++ e headers"""
        extensions = {".cpp", ".hpp", ".h", ".cc", ".cxx", ".hxx"}
        files = []
        for ext in extensions:
            files.extend(self.project_path.rglob(f"*{ext}"))
        # Ignora arquivos em build/, .cache/, etc
        files = [
            f
            for f in files
            if not any(
                part.startswith(".") or part == "build"
                for part in f.relative_to(self.project_path).parts
            )
        ]
        return sorted(files)

    def _get_cmake_files(self) -> List[Path]:
        """Encontra arquivos CMake"""
        cmake = list(self.project_path.rglob("CMakeLists.txt"))
        cmake.extend(self.project_path.rglob("*.cmake"))
        return sorted(cmake)

    def _read_file(self, file_path: Path) -> str:
        """Le conteudo de um arquivo"""
        try:
            return file_path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            return file_path.read_text(encoding="latin-1")

    def review_file(self, file_path: Path) -> Dict:
        """Faz review detalhado de um arquivo"""
        code = self._read_file(file_path)
        relative_path = file_path.relative_to(self.project_path)

        prompt = f"""Voce e um especialista em C++ fazendo code review do projeto bidi-x.

{self.project_context}

{self._get_review_rules()}

ARQUIVO PARA REVIEW: {relative_path}

```cpp
{code}
```

INSTRUCOES DE REVIEW:
1. Analise o codigo seguindo TODAS as 40+ regras acima
2. Identifique VIOLACOES CRITICAS (que quebram o sistema)
3. Liste problemas de severidade ALTA, MEDIA e BAIXA
4. De sugestoes concretas de correcao com trechos de codigo
5. Identifique pontos positivos (para reforcar boas praticas)
6. Atribua score (0-10) baseado em:
   - Conformidade com arquitetura bidi-x (peso 40%)
   - Adherencia a Core Guidelines & P10 (peso 30%)
   - Performance & memoria (peso 20%)
   - Qualidade & testes (peso 10%)

FORMATO DA RESPOSTA:
## VIOLACOES CRITICAS
[liste aqui ou "Nenhuma"]

## PROBLEMAS ALTA SEVERIDADE
[liste com linhas especificas]

## PROBLEMAS MEDIA SEVERIDADE
[liste com linhas especificas]

## PROBLEMAS BAIXA SEVERIDADE
[liste com linhas especificas]

## SUGESTOES DE CORRECAO
[codigo concreto]

## PONTOS POSITIVOS
[liste]

## SCORE FINAL: X/10
[justificativa]"""

        try:
            message = self.client.messages.create(
                model="claude-sonnet-4-20250514",
                max_tokens=8192,
                messages=[{"role": "user", "content": prompt}],
            )

            review = {
                "file": str(relative_path),
                "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
                "review": message.content[0].text,
                "tokens_used": message.usage.input_tokens + message.usage.output_tokens,
            }

            return review

        except Exception as e:
            return {
                "file": str(relative_path),
                "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
                "error": str(e),
            }

    def run_review_loop(self, max_files: Optional[int] = None, delay: int = 2):
        """
        Executa review em loop dos arquivos C++

        Args:
            max_files: Limite de arquivos (None = todos)
            delay: Delay em segundos entre reviews
        """
        cpp_files = self._get_cpp_files()

        if not cpp_files:
            print("Nenhum arquivo C++ encontrado")
            return

        if max_files:
            cpp_files = cpp_files[:max_files]

        print(f"Encontrados {len(cpp_files)} arquivos C++ para review")
        print(f"Contexto carregado: {len(self.project_context)} chars\n")

        critical_violations = []

        for i, file_path in enumerate(cpp_files, 1):
            print(f"\n{'='*70}")
            print(f"[{i}/{len(cpp_files)}] {file_path.name}")
            print(f"{'='*70}")

            review = self.review_file(file_path)
            self.review_history.append(review)

            if "error" in review:
                print(f"ERRO: {review['error']}")
            else:
                print(review["review"])
                print(f"\nTokens: {review['tokens_used']}")

                # Detecta violacoes criticas
                if "VIOLACOES CRITICAS" in review["review"]:
                    content = (
                        review["review"].split("VIOLACOES CRITICAS")[1].split("\n")[1]
                    )
                    if "Nenhuma" not in content:
                        critical_violations.append(
                            {
                                "file": str(file_path.relative_to(self.project_path)),
                                "violations": content,
                            }
                        )

            self._save_progress()

            if i < len(cpp_files):
                time.sleep(delay)

        print(f"\n\n{'='*70}")
        print("REVIEW COMPLETO")
        print(f"{'='*70}")
        self._print_summary(critical_violations)

    def _save_progress(self):
        """Salva progresso em JSON"""
        output = self.project_path / "bidi_x_code_review.json"
        with open(output, "w", encoding="utf-8") as f:
            json.dump(self.review_history, f, indent=2, ensure_ascii=False)

    def _print_summary(self, critical_violations: List[Dict]):
        """Imprime sumario do review"""
        total = len(self.review_history)
        errors = sum(1 for r in self.review_history if "error" in r)
        success = total - errors

        print(f"\nESTATISTICAS:")
        print(f"  Total de arquivos: {total}")
        print(f"  Revisados com sucesso: {success}")
        print(f"  Erros: {errors}")

        if critical_violations:
            print(
                f"\n  ALERTA: {len(critical_violations)} arquivo(s) com VIOLACOES CRITICAS:"
            )
            for v in critical_violations:
                print(f"    - {v['file']}")

        print(f"\nResultados salvos em: bidi_x_code_review.json")


def main():
    """Funcao principal"""
    API_KEY = os.getenv("ANTHROPIC_API_KEY")
    if not API_KEY:
        print("ERRO: defina ANTHROPIC_API_KEY")
        return

    PROJECT_PATH = input("Caminho do projeto bidi-x: ").strip()
    if not PROJECT_PATH:
        PROJECT_PATH = "."

    max_files_input = input("Limite de arquivos (Enter para todos): ").strip()
    max_files = int(max_files_input) if max_files_input else None

    print("\nIniciando code review especializado bidi-x...\n")

    reviewer = BidiXCodeReviewer(API_KEY, PROJECT_PATH)
    reviewer.run_review_loop(max_files=max_files, delay=2)

    # Salva cada resultado individualmente em llm_review/arquivo.md
    llm_review_dir = Path(PROJECT_PATH) / "llm_review"
    llm_review_dir.mkdir(exist_ok=True)

    for review in reviewer.review_history:
        file_name = review.get("file", "unknown_file").replace("/", "__")
        output_path = llm_review_dir / f"{file_name}.md"
        with open(output_path, "w", encoding="utf-8") as f:
            if "review" in review:
                f.write(review["review"])
            elif "error" in review:
                f.write(f"ERRO: {review['error']}")


if __name__ == "__main__":
    main()
