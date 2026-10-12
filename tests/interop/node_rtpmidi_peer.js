// The other end of an AppleMIDI session, played by node-rtpmidi
// (https://github.com/jdachtera/node-rtpmidi), an implementation that shares
// no code with ours. It does not implement the recovery journal: it sends
// none and ignores ours.
//
//   node node_rtpmidi_peer.js PORT [ADDRESS REMOTE_PORT]
//
// With an address it invites that session; without one it waits to be
// invited. Four seconds after a stream is up (the package drops what it is
// asked to send before its first clock exchange is over) it sends a Note
// On, a Note Off, a Control Change and a short SysEx, and it prints one
// line for everything it sees.
// NODE_PATH has to lead to the installed package.
const rtpmidi = require('rtpmidi');

const port = parseInt(process.argv[2], 10);
const address = process.argv[3];
const remotePort = parseInt(process.argv[4] || '0', 10);

function hex(bytes) {
  return Array.from(bytes).map((b) => b.toString(16).padStart(2, '0')).join('');
}

const session = rtpmidi.manager.createSession({
  localName: process.env.PEER_NAME || 'Node peer',
  bonjourName: process.env.PEER_NAME || 'Node peer',
  port,
  published: false,
}, true);

session.on('ready', () => {
  console.log(`ready port=${port}`);
  if (address) session.connect({ address, port: remotePort });
});
session.on('streamAdded', (event) => {
  const { stream } = event;
  console.log(`stream name=${JSON.stringify(stream.name)} ssrc=${stream.ssrc}`);
  setTimeout(() => {
    // In version 1.0.0 sendMessage() works only with a time and a plain
    // array: its one-argument form and a Buffer both leave it without the
    // command. The time is now, in the session's own clock.
    const send = (bytes) => session.sendMessage(
      session.startTime + session.now(), bytes,
    );
    send([0x90, 0x45, 0x50]);
    send([0x80, 0x45, 0x40]);
    send([0xb0, 0x07, 0x64]);
    send([0xf0, 0x7d, 0x01, 0x02, 0x03, 0xf7]);
    console.log('sent');
  }, 4000);
});
session.on('streamRemoved', () => console.log('stream removed'));
session.on('message', (deltaTime, message) => {
  console.log(`message ${hex(message)}`);
});
session.on('controlMessage', (message) => {
  console.log(`control ${message.command}`);
});
session.on('error', (error) => console.log(`error ${error}`));

process.on('SIGTERM', () => {
  session.end(() => process.exit(0));
  setTimeout(() => process.exit(0), 500);
});
