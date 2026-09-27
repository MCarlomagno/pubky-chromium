// META: global=dedicatedworker

test(() => {
  assert_false('pubky' in navigator);
  assert_false('PubkyCapabilities' in self);
}, 'The Pubky capability API is Window-only');
