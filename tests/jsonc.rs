use jefetch::config::parse;

#[test]
fn parse_basic_object() {
    let v = parse(r#"{"name": "fastfetch", "count": 5, "ok": true}"#).unwrap();
    assert_eq!(v.get("name").unwrap().as_str(), Some("fastfetch"));
    assert_eq!(v.get("count").unwrap().as_u64(), Some(5));
    assert_eq!(v.get("ok").unwrap().as_bool(), Some(true));
}

#[test]
fn parse_comments_and_trailing_commas() {
    let v = parse(
        r#"{
            // line comment
            "logo": { "type": "nixos", /* inline */ },
            "modules": ["title", "separator",],
        }"#,
    )
    .unwrap();
    assert_eq!(
        v.get_path(&["logo", "type"]).unwrap().as_str(),
        Some("nixos")
    );
    assert_eq!(v.get_path(&["modules"]).unwrap().arr().unwrap().len(), 2);
}

#[test]
fn parse_string_escapes() {
    let v = parse(r#""a\nb\t\"c\\d\u0041""#).unwrap();
    assert_eq!(v.as_str(), Some("a\nb\t\"c\\dA"));
}

#[test]
fn parse_surrogate_pair() {

    let v = parse(r#""\uD83D\uDE00""#).unwrap();
    assert_eq!(v.as_str(), Some("\u{1F600}"));
}

#[test]
fn parse_numbers() {
    let v = parse("[0, -1, 3.14, 1e3, 2.5e-2]").unwrap();
    let a = v.arr().unwrap();
    assert_eq!(a[0].as_u64(), Some(0));
    assert_eq!(a[1].as_i64(), Some(-1));
    assert!((a[2].as_f64().unwrap() - 3.14).abs() < 1e-9);
    assert!((a[3].as_f64().unwrap() - 1000.0).abs() < 1e-9);
    assert!((a[4].as_f64().unwrap() - 0.025).abs() < 1e-9);
}

#[test]
fn parse_nested() {
    let v = parse(r#"{"a": {"b": {"c": [1, [2, 3], {"d": null}]}}}"#).unwrap();
    let inner = v.get_path(&["a", "b", "c"]).unwrap();
    let arr = inner.arr().unwrap();
    assert_eq!(arr.len(), 3);
    assert!(arr[2].get("d").unwrap().is_null());
}

#[test]
fn parse_errors_on_bad_input() {
    assert!(parse("{").is_err());
    assert!(parse("[1 2]").is_err());
    assert!(parse("{\"a\": }").is_err());
    assert!(parse("tru").is_err());
}

#[test]
fn parse_array_of_objects() {
    let v = parse(r#"[{"type":"os"},{"type":"kernel"}]"#).unwrap();
    let a = v.arr().unwrap();
    assert_eq!(a[0].get("type").unwrap().as_str(), Some("os"));
    assert_eq!(a[1].get("type").unwrap().as_str(), Some("kernel"));
}
