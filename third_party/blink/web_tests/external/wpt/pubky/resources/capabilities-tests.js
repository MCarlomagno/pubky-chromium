'use strict';

test(() => {
  assert_true('pubky' in navigator);
  assert_true(navigator.pubky instanceof PubkyCapabilities);
  assert_equals(Object.prototype.toString.call(navigator.pubky),
                '[object PubkyCapabilities]');
}, 'Navigator exposes a native PubkyCapabilities object');

test(() => {
  assert_equals(navigator.pubky.supportsPkdns, true);
  assert_equals(navigator.pubky.supportsRawPublicKeyTls, true);
}, 'Capability getters report built-in PKDNS and RFC 7250 support');

test(() => {
  assert_equals(navigator.pubky, navigator.pubky);
}, 'navigator.pubky is SameObject');

test(() => {
  assert_equals(Object.getOwnPropertyDescriptor(Navigator.prototype, 'pubky').set,
                undefined);
  assert_throws_js(TypeError, () => { navigator.pubky = null; });
  for (const property of ['supportsPkdns', 'supportsRawPublicKeyTls']) {
    assert_equals(Object.getOwnPropertyDescriptor(
        PubkyCapabilities.prototype, property).set, undefined);
    assert_throws_js(TypeError, () => { navigator.pubky[property] = false; });
    assert_equals(navigator.pubky[property], true);
  }
}, 'The capability object and its booleans have read-only WebIDL attributes');

test(() => {
  assert_throws_js(TypeError, () => new PubkyCapabilities());
}, 'PubkyCapabilities cannot be constructed by script');

promise_test(async t => {
  const frame = document.createElement('iframe');
  const loaded = new Promise(resolve => { frame.onload = resolve; });
  t.add_cleanup(() => frame.remove());
  document.body.appendChild(frame);
  await loaded;
  const capabilities = frame.contentWindow.navigator.pubky;
  assert_not_equals(capabilities, navigator.pubky);
  assert_equals(capabilities, frame.contentWindow.navigator.pubky);
  assert_equals(capabilities.supportsPkdns, true);
  frame.remove();
  assert_equals(capabilities.supportsRawPublicKeyTls, true);
}, 'Each Navigator owns its capability object, which survives frame detachment');
