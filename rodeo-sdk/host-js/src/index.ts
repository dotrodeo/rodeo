// The host protocol's computer side: rodeo-sdk/docs/guides/host.mdx.
// rodeo-demo's own part is a separate import: '@dotrodeo/rodeo-host/rodeo-demo'.
export * from './protocol.ts';
export { MidiParser, messageLength } from './parser.ts';
export { RodeoClient, type Snapshot, type Status, type Timers, type Transport } from './client.ts';
