# `Std.Net.Dns.Message`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.Message`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.Message`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Message** · [`export const TypeA: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L28)
  Type A, an IPv4 address.
- **Message** · [`export const TypeNs: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L30)
  Type NS, an authoritative name server.
- **Message** · [`export const TypeCname: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L32)
  Type CNAME, the canonical name of an alias.
- **Message** · [`export const TypeSoa: int = 6`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L34)
  Type SOA, the start of a zone of authority.
- **Message** · [`export const TypePtr: int = 12`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L36)
  Type PTR, a domain name pointer.
- **Message** · [`export const TypeMx: int = 15`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L38)
  Type MX, a mail exchange.
- **Message** · [`export const TypeTxt: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L40)
  Type TXT, text strings.
- **Message** · [`export const TypeAaaa: int = 28`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L42)
  Type AAAA, an IPv6 address (RFC 3596).
- **Message** · [`export const TypeSrv: int = 33`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L44)
  Type SRV, a service location (RFC 2782).
- **Message** · [`export const TypeNaptr: int = 35`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L46)
  Type NAPTR, a naming authority pointer (RFC 3403).
- **Message** · [`export const TypeOpt: int = 41`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L48)
  Type OPT, the EDNS(0) pseudo-record (RFC 6891).
- **Message** · [`export const TypeAny: int = 255`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L50)
  QTYPE ANY (`*`).
- **Message** · [`export const ClassIn: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L52)
  Class IN, the Internet.
- **Message** · [`export const OpcodeQuery: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L54)
  Opcode QUERY.
- **Message** · [`export const RcodeNoError: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L56)
  RCODE NOERROR.
- **Message** · [`export const RcodeFormatError: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L58)
  RCODE FORMERR.
- **Message** · [`export const RcodeServerFailure: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L60)
  RCODE SERVFAIL.
- **Message** · [`export const RcodeNameError: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L62)
  RCODE NXDOMAIN, the name does not exist.
- **Message** · [`export const RcodeNotImplemented: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L64)
  RCODE NOTIMP.
- **Message** · [`export const RcodeRefused: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L66)
  RCODE REFUSED.
- **Message** · [`export const MaxUdpSize: int = 512`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L68)
  The largest UDP message without EDNS (RFC 1035 section 2.3.4).
- **Message** · [`export const HeaderLength: int = 12`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L70)
  The header length.
- **Message** · [`export let EdnsOption = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L74)
  One EDNS option: a code and its bytes (RFC 6891 section 6.1.2). Tagged `#Final`.
- **Message.EdnsOption** · [`export last code: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L76)
  The option code.
- **Message.EdnsOption** · [`export last data: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L78)
  The option data.
- **Message.EdnsOption** · [`export let init = func(option_code: int, option_data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L81)
  An option; a code outside 0-65535 traps with "invalid argument: option code".
- **Message.EdnsOption** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L90)
  Equal when code and bytes are.
- **Message.EdnsOption** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L98)
  Combines the code and the bytes.
- **Message.EdnsOption** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L101)
  Text such as "EdnsOption(10, 8 bytes)".
- **Message** · [`export let Edns = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L107)
  The EDNS(0) parameters an OPT record carries (RFC 6891 section 6.1.3). Tagged `#Final`.
- **Message.Edns** · [`export last udp_payload_size: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L109)
  The requester's UDP payload size (the OPT CLASS field).
- **Message.Edns** · [`export last extended_rcode: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L111)
  The upper 8 bits of the 12-bit RCODE.
- **Message.Edns** · [`export last version: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L113)
  The EDNS version (0).
- **Message.Edns** · [`export last dnssec_ok: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L115)
  The DO bit (RFC 3225).
- **Message.Edns** · [`export last options: EdnsOption[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L117)
  The options.
- **Message.Edns** · [`export let init = func(payload_size: int, rcode_high: int, edns_version: int, dnssec: bool, edns_options: EdnsOption[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L120)
  EDNS parameters. Fields out of range trap with "invalid argument: edns".
- **Message.Edns** · [`export let ttl_field = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L135)
  The TTL field of the OPT record. Tagged `#NoDiscard`.
- **Message.Edns** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L139)
  Equal when every field and option is.
- **Message.Edns** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L154)
  Combines the fields.
- **Message.Edns** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L164)
  Text such as "Edns(udp 1232, version 0, do false, 0 options)".
- **Message** · [`export let edns_of = func(payload_size: int) -> Edns`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L170)
  EDNS(0) with a UDP payload size, version 0, no DO bit and no options. Tagged `#NoDiscard`.
- **Message** · [`export let RecordData = enum ( A(Ipv4Addr), Aaaa(Ipv6Addr), Ns(names.DnsName), Cname(names.DnsName), Ptr(names.DnsName), Mx { preference: int, exchange: names.DnsName }, Txt(uint8[list][list]), Soa { mname: names.DnsName, rname: names.DnsName, serial: int64, refresh: int64, retry: int64, expire: int64, minimum: int64 },  Srv { priority: int, weight: int, port: int, target: names.DnsName }, Naptr { order: int, preference: int, flags: string, services: string, regexp: string, replacement: names.DnsName },  Opt(EdnsOption[list]), Unknown(uint8[list]) )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L173)
  The data of one resource record.
- **Message** · [`export let Question = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L194)
  One question: a name, a QTYPE and a raw QCLASS (16 bits; mDNS uses the top bit). Tagged `#Final`.
- **Message.Question** · [`export last name: names.DnsName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L196)
  The name asked about.
- **Message.Question** · [`export last qtype: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L198)
  The QTYPE.
- **Message.Question** · [`export last qclass: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L200)
  The raw QCLASS.
- **Message.Question** · [`export let init = func(asked: names.DnsName, question_type: int, question_class: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L203)
  A question; a type or class outside 0-65535 traps with "invalid argument: question".
- **Message.Question** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L213)
  Equal when the name (ignoring case), type and class are.
- **Message.Question** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L221)
  Combines the name, type and class.
- **Message.Question** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L225)
  Text such as "www.example.com. IN A".
- **Message** · [`export let ResourceRecord = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L231)
  One resource record: owner name, type, raw class, TTL (unsigned 32-bit) and data. Tagged `#Final`.
- **Message.ResourceRecord** · [`export last name: names.DnsName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L233)
  The owner name.
- **Message.ResourceRecord** · [`export last rtype: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L235)
  The TYPE.
- **Message.ResourceRecord** · [`export last rclass: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L237)
  The raw CLASS (16 bits).
- **Message.ResourceRecord** · [`export last ttl: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L239)
  The TTL in seconds, 0 to 2^32-1.
- **Message.ResourceRecord** · [`export last data: RecordData`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L241)
  The data.
- **Message.ResourceRecord** · [`export let init = func(owner: names.DnsName, record_type: int, record_class: int, time_to_live: int64, record_data: RecordData)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L245)
  A record. A type or class outside 0-65535, or a TTL outside 0 to 2^32-1, traps with "invalid argument: record".
- **Message.ResourceRecord** · [`export let with_ttl = func(time_to_live: int64) -> ResourceRecord`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L260)
  The same record with another TTL. Tagged `#NoDiscard`.
- **Message.ResourceRecord** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L264)
  Equal when owner (ignoring case), type, class, TTL and data are.
- **Message.ResourceRecord** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L273)
  Combines the owner, type, class, TTL and the data's text.
- **Message.ResourceRecord** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L281)
  Master-file-like text: "www.example.com. 300 IN A 192.0.2.1".
- **Message** · [`export let Header = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L288)
  The header fields (RFC 1035 section 4.1.1). The section counts are filled by `decode` and recomputed by `encode`. Tagged `#Final`, `#NotThreadSafe`.
- **Message.Header** · [`export let id: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L290)
  The 16-bit ID.
- **Message.Header** · [`export let is_response: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L292)
  QR: a response.
- **Message.Header** · [`export let opcode: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L294)
  The 4-bit opcode.
- **Message.Header** · [`export let authoritative: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L296)
  AA: an authoritative answer.
- **Message.Header** · [`export let truncated: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L298)
  TC: truncated.
- **Message.Header** · [`export let recursion_desired: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L300)
  RD: recursion desired.
- **Message.Header** · [`export let recursion_available: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L302)
  RA: recursion available.
- **Message.Header** · [`export let z: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L304)
  The Z bit (bit 9; must be zero).
- **Message.Header** · [`export let authentic_data: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L306)
  AD: authentic data (RFC 4035).
- **Message.Header** · [`export let checking_disabled: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L308)
  CD: checking disabled (RFC 4035).
- **Message.Header** · [`export let rcode: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L310)
  The 4-bit RCODE (the low bits of the extended RCODE with EDNS).
- **Message.Header** · [`export let question_count: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L312)
  QDCOUNT as decoded.
- **Message.Header** · [`export let answer_count: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L314)
  ANCOUNT as decoded.
- **Message.Header** · [`export let authority_count: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L316)
  NSCOUNT as decoded.
- **Message.Header** · [`export let additional_count: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L318)
  ARCOUNT as decoded (the OPT record included).
- **Message.Header** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L321)
  A header with ID 0 and every flag clear.
- **Message.Header** · [`export let flags = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L326)
  The 16-bit flags word. Tagged `#NoDiscard`.
- **Message.Header** · [`export let set_flags = func(word: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L337)
  Sets every flag from a 16-bit flags word.
- **Message.Header** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L351)
  Text such as "Header(id 4660, flags 0x0100)".
- **Message** · [`export let Message = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L357)
  A whole DNS message. Tagged `#Final`, `#NotThreadSafe`.
- **Message.Message** · [`export last header: Header = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L359)
  The header.
- **Message.Message** · [`export last questions: Question[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L361)
  The question section.
- **Message.Message** · [`export last answers: ResourceRecord[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L363)
  The answer section.
- **Message.Message** · [`export last authorities: ResourceRecord[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L365)
  The authority section.
- **Message.Message** · [`export last additionals: ResourceRecord[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L367)
  The additional section without the OPT record.
- **Message.Message** · [`export let edns: Optional<Edns> = Optional<Edns>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L369)
  The EDNS(0) parameters, from or for the OPT record.
- **Message.Message** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L372)
  An empty message (ID 0, a query).
- **Message.Message** · [`export let rcode = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L377)
  The full RCODE: the header's 4 bits plus EDNS's upper 8 bits. Tagged `#NoDiscard`.
- **Message.Message** · [`export let question = func -> Question`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L384)
  The first question; traps with "invalid argument: no question" when there is none. Tagged `#NoDiscard`.
- **Message.Message** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L392)
  Text listing the header and section sizes.
- **Message** · [`export let query = func(id: int, name: names.DnsName, qtype: int) -> Message`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L399)
  A recursive query for `name` and `qtype` in class IN with `id`. Tagged `#NoDiscard`.
- **Message** · [`export let response_to = func(request: Message, rcode: int) -> Message`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L409)
  A response to `request`: same ID, opcode, RD and questions, QR set, the given RCODE. Tagged `#NoDiscard`.
- **Message** · [`export let encode = func(message: Message) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L425)
  Encodes a message. Err (Custom `dns.too_large`) when it would exceed 65,535 bytes or a section exceeds 65,535 entries. Tagged `#NoDiscard`.
- **Message** · [`export let decode = func(bytes: uint8[list]) -> Result<Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L474)
  Decodes a message. Err (Parse) for a truncated or malformed message, a bad name, rdata that does not use exactly RDLENGTH, more than one OPT record or trailing bytes. Tagged `#NoDiscard`.
- **Message** · [`export let rdata_bytes = func(data: RecordData) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L521)
  The record data as raw bytes, as RFC 3597 section 5 writes it for an unknown type (uncompressed). Tagged `#NoDiscard`.
- **Message** · [`export let txt_strings = func(data: RecordData) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L529)
  The character-strings of TXT data as text (invalid UTF-8 replaced); empty for other data. Tagged `#NoDiscard`.
- **Message** · [`export let txt_of = func(texts: string[list]) -> RecordData`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L545)
  TXT data of text strings; a string above 255 bytes traps with "invalid argument: character-string". Tagged `#NoDiscard`.
- **Message** · [`export let type_name = func(rtype: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L559)
  The mnemonic of a type ("A", "AAAA", ...), or "TYPEn" (RFC 3597 section 5). Tagged `#NoDiscard`.
- **Message** · [`export let class_name = func(rclass: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L590)
  "IN" for class 1, otherwise "CLASSn" (RFC 3597 section 5). Tagged `#NoDiscard`.
- **Message** · [`export let data_text = func(data: RecordData) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/message.nra#L594)
  The master-file-like text of record data; unknown data as `\# length hex` (RFC 3597 section 5). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
