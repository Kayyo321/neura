# `Std.Net.Http.Fetch`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Fetch`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Fetch`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)
- [The `neura.http.fetch` service: arguments, error codes, CORS and limits](Providers#http-fetch)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Fetch** · [`export let Credentials = enum (Omit, SameOrigin, Include)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L34)
  Whether the browser sends cookies and HTTP authentication with the request. Since 0.3.0.
- **Fetch** · [`export let FetchFailure = enum ( InvalidInput, TimedOut, Unsupported, CapabilityDenied, Cancelled, TooLarge, Busy, OutcomeUnknown, Other, RedirectDenied, NotProcessed )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L38)
  Why a fetch produced no response. RedirectDenied and NotProcessed are new in 0.3.1. Since 0.3.0.
- **Fetch** · [`export const DefaultTimeoutSeconds: int64 = 30`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L52)
  The default timeout of a request, 30 seconds, as for generated clients.
- **Fetch** · [`export let Response = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L57)
  An HTTP response: the status, the header pairs the host exposes (in a browser, lowercase names, and for a cross-origin response only those CORS exposes), and the body bytes. Tagged `#Final`. Since 0.3.0.
- **Fetch.Response** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L59)
  The HTTP status code.
- **Fetch.Response** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L61)
  The response body.
- **Fetch.Response** · [`export let init = func(code: int, header_pairs: [string, string][list], bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L65)
  Creates a response from its parts.
- **Fetch.Response** · [`export let ok = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L73)
  Whether the status is 200 to 299. Tagged `#Pure`.
- **Fetch.Response** · [`export let headers = func -> [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L77)
  The header pairs in the host's order. Tagged `#Pure`.
- **Fetch.Response** · [`export let header = func(name: string) -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L87)
  The value of the first header with this name, compared without regard to ASCII case. Tagged `#Pure`.
- **Fetch.Response** · [`export let text = func -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L98)
  The body as UTF-8 text, or an Encoding error. Tagged `#NoDiscard`.
- **Fetch.Response** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L102)
  The status and the body's size. Tagged `#Pure`.
- **Fetch** · [`export let Request = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L109)
  A request to send: a method, a URL (absolute, or relative to the page in a browser), headers, a body, a timeout and a credentials mode. Each `with_` member returns a new request. Tagged `#Final`. Since 0.3.0.
- **Fetch.Request** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L111)
  The method, such as GET or POST.
- **Fetch.Request** · [`export last url: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L113)
  The URL.
- **Fetch.Request** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L115)
  The body; empty for none.
- **Fetch.Request** · [`export last timeout: Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L117)
  The longest the exchange may take. Zero or less uses the host's default.
- **Fetch.Request** · [`export last credentials: Credentials`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L119)
  The credentials mode.
- **Fetch.Request** · [`export let init = func(verb: string, target: string, header_pairs: [string, string][list], bytes: uint8[list], limit: Duration, mode: Credentials)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L123)
  Creates a request from all of its parts.
- **Fetch.Request** · [`export let headers = func -> [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L135)
  The header pairs in the order they were added. Tagged `#Pure`.
- **Fetch.Request** · [`export let with_header = func(name: string, value: string) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L139)
  The same request with one more header. Tagged `#Pure`.
- **Fetch.Request** · [`export let with_body = func(bytes: uint8[list]) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L147)
  The same request with this body. Tagged `#Pure`.
- **Fetch.Request** · [`export let with_text = func(content_type: string, text: string) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L152)
  The same request with a text body and a Content-Type header. Tagged `#Pure`.
- **Fetch.Request** · [`export let with_timeout = func(limit: Duration) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L160)
  The same request with this timeout. Tagged `#Pure`.
- **Fetch.Request** · [`export let with_credentials = func(mode: Credentials) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L164)
  The same request with this credentials mode. Tagged `#Pure`.
- **Fetch.Request** · [`export let send = func -> result.Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L169)
  Sends the request and waits for the response. Tagged `#NoDiscard`.
- **Fetch.Request** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L180)
  The method and URL. Tagged `#Pure`.
- **Fetch** · [`export let request = func(method: string, url: string) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L185)
  A request with no headers or body, the default timeout, and same-origin credentials. Tagged `#Pure`.
- **Fetch** · [`export let get = func(url: string) -> result.Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L193)
  Sends a GET request with the defaults of `request`. Tagged `#NoDiscard`.
- **Fetch** · [`export let post_text = func(url: string, content_type: string, text: string) -> result.Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L197)
  Sends a POST request with a text body of the content type, and the defaults of `request`. Tagged `#NoDiscard`.
- **Fetch** · [`export let failure_of = func(e: Error) -> FetchFailure`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/fetch.nra#L202)
  Classifies an error that `send` returned; any other error is Other. Tagged `#Pure`.
<!-- END SOURCE API -->
