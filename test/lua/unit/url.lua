-- URL parser tests

context("URL check functions", function()
  local mpool = require("rspamd_mempool")
  local url = require("rspamd_url")
  local logger = require("rspamd_logger")
  local test_helper = require("rspamd_test_helper")
  local ffi = require("ffi")

  ffi.cdef[[
  void rspamd_http_normalize_path_inplace(char *path, size_t len, size_t *nlen);
  ]]

  test_helper.init_url_parser()

  local pool = mpool.create()

  local cases = {
    {"test.com", {"test.com", nil}},
    {" test.com", {"test.com", nil}},
    {"<test.com> text", {"test.com", nil}},
    {"test.com. text", {"test.com", nil}},
    {"mailto:A.User@example.com text", {"example.com", "A.User"}},
    {"http://Тест.Рф:18 text", {"тест.рф", nil}},
    {"http://user:password@тест2.РФ:18 text", {"тест2.рф", "user"}},
    {"somebody@example.com", {"example.com", "somebody"}},
    {"https://127.0.0.1/abc text", {"127.0.0.1", nil}},
    {"https:\\\\127.0.0.1/abc text", {"127.0.0.1", nil}},
    {"https:\\\\127.0.0.1", {"127.0.0.1", nil}},
    {"https://127.0.0.1 text", {"127.0.0.1", nil}},
    {"https://[::1]:1", {"::1", nil}},
    {"https://user:password@[::1]:1", {"::1", nil}},
    {"https://user:password@[::1]", {"::1", nil}},
    {"https://user:password@[::1]/1", {"::1", nil}},
  }

  for i,c in ipairs(cases) do
    local res = url.create(pool, c[1])

    test("Extract urls from text" .. i, function()
      assert_not_nil(res, "cannot parse " .. c[1])
      local t = res:to_table()
      --local s = logger.slog("%1 -> %2", c[1], t)
      --print(s)
      assert_not_nil(t, "cannot convert to table " .. c[1])
      assert_equal(c[2][1], t['host'],
              logger.slog('expected host "%s", but got "%s" in url %s => %s',
              c[2][1], t['host'], c[1], t))

      if c[2][2] then
        assert_equal(c[2][1], t['host'],
                logger.slog('expected user "%s", but got "%s" in url %s => %s',
                        c[2][1], t['host'], c[1], t))
      end
    end)
  end

  cases = {
    {'http://example.net/?arg=%23#fragment', true, {
      host = 'example.net', fragment = 'fragment', query = 'arg=#',
    }},
    {"http:/\\[::eeee:192.168.0.1]/#test", true, {
      host = '::eeee:c0a8:1', fragment = 'test'
    }},
    {"http:/\\[::eeee:192.168.0.1]#test", true, {
      host = '::eeee:c0a8:1', fragment = 'test'
    }},
    {"http:/\\[::eeee:192.168.0.1]?test", true, {
      host = '::eeee:c0a8:1', query = 'test'
    }},
    {"http:\\\\%30%78%63%30%2e%30%32%35%30.01", true, { --0xc0.0250.01
      host = '192.168.0.1',
    }},
    {"http:/\\www.google.com/foo?bar=baz#", true, {
      host = 'www.google.com', path = 'foo', query = 'bar=baz', tld = 'google.com'
    }},
    {"http://[www.google.com]/", false},
    {"<test.com", true, {
      host = 'test.com', tld = 'test.com',
    }},
    {"test.com>", false},
    {",test.com text", false},
    {"ht\ttp:@www.google.com:80/;p?#", false},
    {"http://user:pass@/", false},
    {"http://foo:-80/", false},
    {"http:////////user:@google.com:99?foo", true, {
      host = 'google.com', user = 'user', port = 99, query = 'foo'
    }},
    {"http://%25DOMAIN:foobar@foodomain.com/", true, {
      host = 'foodomain.com', user = '%25DOMAIN'
    }},
    {"http://0.0xFFFFFF", true, {
      host = '0.255.255.255'
    }},
    {"http:/\\030052000001", true, {
      host = '192.168.0.1'
    }},
    {"http:\\/0xc0.052000001", true, {
      host = '192.168.0.1'
    }},
    {"http://192.168.0.1.?foo", true, {
      host = '192.168.0.1', query = 'foo',
    }},
    {"http://twitter.com#test", true, {
      host = 'twitter.com', fragment = 'test'
    }},
    {"http:www.twitter.com#test", true, {
      host = 'www.twitter.com', fragment = 'test'
    }},
  }

  -- Some cases from https://code.google.com/p/google-url/source/browse/trunk/src/url_canon_unittest.cc
  for i,c in ipairs(cases) do
    local res = url.create(pool, c[1])

    test("Parse url: " .. c[1], function()
      if c[2] then
        assert_not_nil(res, "we are able to parse url: " .. c[1])

        local uf = res:to_table()

        for k,v in pairs(c[3]) do
          assert_not_nil(uf[k], k .. ' is missing in url, must be ' .. v)
          assert_equal(uf[k], v, logger.slog('expected "%s", for %s, but got "%s" in url %s => %s',
                v, k, uf[k], c[1], uf))
        end
        for k,v in pairs(uf) do
          if k ~= 'url' and k ~= 'protocol' and k ~= 'tld' then
            assert_not_nil(c[3][k], k .. ' should be absent but it is ' .. v .. ' in: ' .. c[1])
          end
        end
      else
        assert_nil(res, "should not parse " .. c[1] .. ' parsed to: ' .. tostring(res))
      end
    end)
  end

  cases = {
    {"/././foo", "/foo"},
    {"/a/b/c/./../../g", "/a/g"},
    {"/./.foo", "/.foo"},
    {"/foo/.", "/foo"},
    {"/foo/./", "/foo"},
    {"/foo/bar/..", "/foo"},
    {"/foo/bar/../", "/foo/"},
    {"/foo/..bar", "/foo/..bar"},
    {"/foo/bar/../ton", "/foo/ton"},
    {"/foo/bar/../ton/../../a", "/a"},
    {"/foo/../../..", "/"},
    {"/foo/../../../ton", "/ton"},
    {"////../..", "/"},
    {"./", ""},
    {"/./", "/"},
    {"/./././././././", "/"},
    {"/", "/"},
    {"/a/b", "/a/b"},
    {"/a/b/", "/a/b/"},
    {"..", "/"},
    {"/../", "/"},
    {"../", "/"},
  }

  for i,v in ipairs(cases) do
    test("Normalize paths " .. i, function()
      local buf = ffi.new("uint8_t[?]", #v[1])
      local sizbuf = ffi.new("size_t[1]")
      ffi.copy(buf, v[1], #v[1])
      ffi.C.rspamd_http_normalize_path_inplace(buf, #v[1], sizbuf)
      local res = ffi.string(buf, tonumber(sizbuf[0]))
      assert_equal(v[2], res, 'expected ' .. v[2] .. ' but got ' .. res .. ' in path ' .. v[1])
    end)
  end
end)
