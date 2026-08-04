Aqui vai um “compilado” prático de **patterns funcionais** — com a ideia central, como aparece em linguagens diferentes e um micro-exemplo quando útil. Coloquei fontes oficiais por trás dos conceitos-chave pra você poder ir fundo depois.

# 1) Imutabilidade e dados persistentes

* **Ideia:** valores não mudam; você cria novas versões de estruturas ao “alterar”.
* **Idiomas/onde ver:** Clojure usa estruturas persistentes (vetores, maps, sets) com operações eficientes; isso é base para pipelines e paralelismo sem dor. ([Clojure][1])

```clojure
(assoc {:a 1} :a 2) ; retorna NOVO map, o antigo continua válido
```

# 2) Funções de ordem superior (map / filter / reduce)

* **Ideia:** transformar, filtrar e dobrar coleções sem loops mutáveis.
* **Idiomas/onde ver:**

  * JavaScript `Array.prototype.map` (e `filter`, `reduce`). ([MDN Web Docs][2])
  * Java Streams (map/filter/reduce). ([Dev.Java][3])
  * C# LINQ `Select`/`Where`. ([Microsoft Learn][4])

```java
List<Integer> xs = List.of(1,2,3);
int somaDobros = xs.stream().map(x -> x*2).reduce(0, Integer::sum);
```

# 3) Composição e “pipelines”

* **Ideia:** encadear transformações como uma linha de montagem.
* **Idiomas/onde ver:**

  * Haskell, composição com `(.)`. ([Computer Science][5])
  * F#/Elixir, **pipe** `|>` para compor “para frente”. ([Microsoft Learn][6])

```elixir
" 42 " |> String.trim() |> String.to_integer() |> Kernel.*(2)
```

# 4) Pattern Matching + ADTs (tipos algébricos)

* **Ideia:** declarar somas/produtos de tipos e fazer *match* estrutural exaustivo.
* **Idiomas/onde ver:** Rust `match` em `enum` (Option/Result), OCaml `match ... with`. ([Documentação do Rust][7])

```rust
enum Shape { Circle(f64), Rect(f64,f64) }
fn area(s: Shape) -> f64 {
  match s { Shape::Circle(r)=>3.14*r*r, Shape::Rect(w,h)=>w*h }
}
```

# 5) Option/Result (Null-safety & erros como dados)

* **Ideia:** substituir `null`/exceções por valores explícitos de sucesso/falha.
* **Idiomas/onde ver:** Rust `Option<T>` e `Result<T,E>` (métodos como `map`, `and_then`). ([Documentação do Rust][8])

```rust
fn parse_i32(s:&str)->Result<i32, std::num::ParseIntError>{ s.parse() }
```

# 6) Functor / Applicative / Monad (mapeando, combinando e encadeando efeitos)

* **Ideia:** padrões de composição para “valores em contextos” (listas, opções, IO, etc.).
* **Idiomas/onde ver:** Haskell (`Functor`, `Applicative`, `Monad`) e Scala Cats. ([Hackage][9])

```haskell
-- Functor: fmap (+1) (Just 2) == Just 3
-- Applicative: (+) <$> Just 2 <*> Just 3 == Just 5
-- Monad: Just 2 >>= (\x -> Just (x*2)) == Just 4
```

# 7) Folds (catamorfismos do dia-a-dia)

* **Ideia:** reduzir uma estrutura a um valor; é o “esqueleto” de muitos algoritmos.
* **Idiomas/onde ver:** Java `Stream.reduce` (fold), DOCUMENTAÇÃO OFICIAL. ([Oracle Documentação][10])

```java
int total = xs.stream().reduce(0, Integer::sum);
```

# 8) Laziness / Streams frios

* **Ideia:** descrever pipelines agora, executar só ao consumir. Economiza memória/tempo.
* **Idiomas/onde ver:** Elixir `Stream` (enumeráveis **preguiçosos**). ([HexDocs][11])

```elixir
1..:infinity |> Stream.map(&(&1*2)) |> Enum.take(5)  # [2,4,6,8,10]
```

# 9) Transducers

* **Ideia:** compor transformações independentes de coleção e sem criar coleções intermediárias.
* **Idiomas/onde ver:** Clojure transducers (definição e uso). ([Clojure][12])

```clojure
(transduce (comp (filter odd?) (map inc)) + (range 10)) ; 25
```

# 10) Lenses/Prisms (acesso imutável a estruturas aninhadas)

* **Ideia:** “ponteiros funcionais” para focar/atualizar partes de estruturas imutáveis.
* **Idiomas/onde ver:** Haskell `lens` (tutorial oficial do pacote). ([Hackage][13])

# 11) FRP / Fluxos assíncronos (Observables, Publishers, Flow)

* **Ideia:** tratar eventos/assíncrono como coleções temporais com operadores (`map`, `filter`, `flatMap`).
* **Idiomas/onde ver:**

  * RxJS (operadores *pipeable*). ([RxJS][14])
  * Swift **Combine** (`map`/`flatMap`). ([Apple Developer][15])
  * Kotlin **Flow** (operadores intermediários e “fluxo frio”). ([Kotlin][16])

```swift
urlPublisher
  .flatMap { URLSession.shared.dataTaskPublisher(for: $0) } // encadeia publishers
  .map(\.data)
```

# 12) Currying e aplicação parcial

* **Ideia:** funções de múltiplos argumentos como cadeia de 1 argumento → facilita composição/pipe.
* **Idiomas/onde ver:** F# dá suporte nativo (funções são curried), composição documentada pela Microsoft. ([Microsoft Learn][17])

```fsharp
let add a b = a + b
let inc = add 1    // aplicação parcial
```

# 13) Recursion Schemes (cata/ana/hylo…)

* **Ideia:** padronizar recursão (dobrar, gerar e gerar-e-dobrar). Útil p/ árvores/ASTs.
* **Onde ver:** guias de catamorfismos/recursion schemes em Haskell. ([Haskell][18])

# 14) Concurrency & Actors “funcionais”

* **Ideia:** isolar estado em processos que trocam mensagens (sem compartilhamento).
* **Idiomas/onde ver:** Erlang/OTP `gen_server` (padrão de servidor genérico) e o equivalente em Elixir `GenServer`. ([Erlang.org][19])

---

## Mini “dicionário” por linguagem (para localizar os patterns)

* **Haskell:** `Functor/Applicative/Monad`, `(.)`, ADTs/pattern matching, `foldr/foldl`, `lens`. ([Hackage][9])
* **Scala (Cats/ZIO):** typeclasses `Functor/Applicative/Monad`, `map/flatMap`. ([Typelevel][20])
* **Rust:** `Option/Result`, `Iterator::map/filter/fold`, `match`. ([Documentação do Rust][8])
* **Elixir:** `|>` pipe, `Enum` (eager) / `Stream` (lazy), `GenServer`. ([HexDocs][21])
* **Clojure:** imutabilidade, transducers, `map/filter/reduce` em `seq`. ([Clojure][1])
* **Java:** `Stream` API (map/filter/reduce). ([Dev.Java][3])
* **C#/.NET:** LINQ `Select/Where/Aggregate`. ([Microsoft Learn][4])
* **Kotlin:** coleções (`map/filter`), *Flow* reativo. ([Kotlin][22])
* **Swift:** `Optional.map/flatMap`, Combine. ([Apple Developer][23])
* **OCaml/F#:** ADTs e *pattern matching* + pipe/composição. ([OCaml][24])

---

## Dicas de uso no dia a dia

1. **Comece por dados imutáveis + `map/filter/reduce`**: dá para refatorar loops aos poucos.
2. **Use Option/Result** para eliminar `null` e exceções não-controladas.
3. **Prefira pipelines** (pipe/composição) para legibilidade.
4. **Quando o volume crescer**, migre para **Streams/Flow/Observables**.
5. **Otimize com transducers** quando vir muita alocação intermediária.
6. **Para estados e I/O complexos**, considere `Applicative/Monad` (ou bibliotecas como Cats/Arrow/FP-TS conforme a linguagem). ([Typelevel][20])


[1]: https://clojure.org/reference/data_structures?utm_source=chatgpt.com "Data Structures"
[2]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Array/map?utm_source=chatgpt.com "Array.prototype.map() - JavaScript | MDN - Mozilla"
[3]: https://dev.java/learn/api/streams/?utm_source=chatgpt.com "The Stream API"
[4]: https://learn.microsoft.com/en-us/dotnet/csharp/linq/get-started/introduction-to-linq-queries?utm_source=chatgpt.com "Introduction to LINQ Queries - C# | Microsoft Learn"
[5]: https://www.cs.arizona.edu/~collberg/Teaching/372/2009/Handouts/Handout-12.pdf?utm_source=chatgpt.com "Haskell — Composing Functions"
[6]: https://learn.microsoft.com/en-us/dotnet/fsharp/language-reference/symbol-and-operator-reference/?utm_source=chatgpt.com "Symbol and Operator Reference - F# | ..."
[7]: https://doc.rust-lang.org/book/ch19-00-patterns.html?utm_source=chatgpt.com "Patterns and Matching - The Rust Programming Language"
[8]: https://doc.rust-lang.org/std/option/enum.Option.html?utm_source=chatgpt.com "Option in std"
[9]: https://hackage.haskell.org/package/base/docs/Control-Applicative.html?utm_source=chatgpt.com "Control.Applicative - Hackage - Haskell.org"
[10]: https://docs.oracle.com/javase/tutorial/collections/streams/reduction.html?utm_source=chatgpt.com "Reduction (The Java™ Tutorials > Collections > Aggregate ..."
[11]: https://hexdocs.pm/elixir/Stream.html?utm_source=chatgpt.com "Stream — Elixir v1.18.4"
[12]: https://clojure.org/reference/transducers?utm_source=chatgpt.com "Transducers"
[13]: https://hackage.haskell.org/package/lens-tutorial/docs/Control-Lens-Tutorial.html?utm_source=chatgpt.com "Control.Lens.Tutorial"
[14]: https://rxjs.dev/guide/operators?utm_source=chatgpt.com "RxJS Operators"
[15]: https://developer.apple.com/documentation/combine/publishers/flatmap?utm_source=chatgpt.com "Publishers.FlatMap | Apple Developer Documentation"
[16]: https://kotlinlang.org/api/kotlinx.coroutines/kotlinx-coroutines-core/kotlinx.coroutines.flow/-flow/?utm_source=chatgpt.com "Flow | kotlinx.coroutines"
[17]: https://learn.microsoft.com/en-us/dotnet/fsharp/language-reference/functions/?utm_source=chatgpt.com "Functions - F# | Microsoft Learn"
[18]: https://www.haskell.org/haskellwiki/catamorphisms?utm_source=chatgpt.com "Catamorphisms - HaskellWiki"
[19]: https://www.erlang.org/docs/24/man/gen_server?utm_source=chatgpt.com "gen_server"
[20]: https://typelevel.org/cats/typeclasses/monad.html?utm_source=chatgpt.com "Monad"
[21]: https://hexdocs.pm/elixir/operators.html?utm_source=chatgpt.com "Operators reference — Elixir v1.18.4"
[22]: https://kotlinlang.org/docs/collection-filtering.html?utm_source=chatgpt.com "Filtering collections | Kotlin Documentation"
[23]: https://developer.apple.com/documentation/swift/optional/flatmap%28_%3A%29?utm_source=chatgpt.com "flatMap(_:) | Apple Developer Documentation"
[24]: https://ocaml.org/manual/patterns.html?utm_source=chatgpt.com "6 Patterns"
