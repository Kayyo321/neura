use biscuit_auth::{
    builder::Term,
    datalog::{ExternFunc, SymbolTable},
    Algorithm, AuthorizerBuilder, Biscuit, BlockBuilder, KeyPair, PublicKey, ThirdPartyRequest,
};
use std::{collections::HashMap, env, fs, path::Path, sync::Arc};

const ISSUER: &str = r#"right("read");"#;
const RESTRICTION: &str = r#"check if operation("read");"#;
const THIRD_CODE: &str = r#"group("admin"); check if right("read");"#;
const AMBIENT: &str = r#"operation("read"); allow if right("read");"#;
const FFI_ISSUER: &str = r#"check if true.extern::test(), "a".extern::test("a") == "equal strings";"#;

fn assert_authorized(token: &Biscuit, code: &str, allowed: bool) {
    assert_authorized_with(AuthorizerBuilder::new(), token, code, allowed);
}

fn assert_authorized_with(builder: AuthorizerBuilder, token: &Biscuit, code: &str, allowed: bool) {
    let result = builder
        .code(code)
        .unwrap()
        .build(token)
        .unwrap()
        .authorize();
    assert_eq!(
        result.is_ok(),
        allowed,
        "unexpected authorization result for {code}: {result:?}"
    );
}

fn record(directory: &Path, name: &str, bytes: &[u8]) {
    fs::write(directory.join(name), bytes).unwrap();
}

fn credential_vectors(directory: &Path) {
    let root = KeyPair::from_bytes(&[1; 32], Algorithm::Secp256r1.into()).unwrap();
    let next = KeyPair::from_bytes(&[2; 32], Algorithm::Ed25519.into()).unwrap();
    let next_third = KeyPair::from_bytes(&[3; 32], Algorithm::Secp256r1.into()).unwrap();
    let next_final = KeyPair::from_bytes(&[4; 32], Algorithm::Ed25519.into()).unwrap();
    let external = KeyPair::from_bytes(&[5; 32], Algorithm::Secp256r1.into()).unwrap();
    let authority = Biscuit::builder()
        .code(ISSUER)
        .unwrap()
        .build_with_key_pair(&root, SymbolTable::new(), &next)
        .unwrap();
    let ffi_authority = Biscuit::builder()
        .code(FFI_ISSUER)
        .unwrap()
        .build_with_key_pair(&root, SymbolTable::new(), &next)
        .unwrap();
    let attenuation = BlockBuilder::new().code(RESTRICTION).unwrap();
    let appended = authority.append_with_keypair(&next_third, attenuation).unwrap();
    let request = appended.third_party_request().unwrap().serialize().unwrap();
    let block = BlockBuilder::new().code(THIRD_CODE).unwrap();
    let response = ThirdPartyRequest::deserialize(&request)
        .unwrap()
        .create_block(&external.private(), block)
        .unwrap();
    let response_bytes = response.serialize().unwrap();
    let third = appended
        .append_third_party_with_keypair(external.public(), response, next_final)
        .unwrap();
    let sealed = third.seal().unwrap();

    let external_scope = format!(
        "operation(\"read\"); allow if group(\"admin\") trusting {};",
        external.public()
    );
    let verify = |token: &Biscuit| Biscuit::from(token.to_vec().unwrap(), root.public()).unwrap();
    assert_authorized(&verify(&authority), AMBIENT, true);
    assert_authorized(&verify(&appended), "allow if true;", false);
    assert_authorized(&verify(&third), &external_scope, true);
    assert_authorized(&verify(&third), r#"operation("read"); allow if group("admin");"#, false);
    assert_authorized(&verify(&sealed), &external_scope, true);
    assert_authorized_with(
        AuthorizerBuilder::new().set_extern_funcs(official_ffi_functions()),
        &verify(&ffi_authority),
        "allow if true;",
        true,
    );
    assert_authorized(&verify(&ffi_authority), "allow if true;", false);

    record(directory, "rust-root.bin", &root.public().to_bytes());
    record(directory, "rust-external.bin", &external.public().to_bytes());
    record(directory, "rust-authority.bc", &authority.to_vec().unwrap());
    record(directory, "rust-ffi-authority.bc", &ffi_authority.to_vec().unwrap());
    record(directory, "rust-attenuated.bc", &appended.to_vec().unwrap());
    record(directory, "rust-request.bin", &request);
    record(directory, "rust-response.bin", &response_bytes);
    record(directory, "rust-third-party.bc", &third.to_vec().unwrap());
    record(directory, "rust-sealed.bc", &sealed.to_vec().unwrap());
    for (index, signature) in sealed.revocation_identifiers().iter().enumerate() {
        record(directory, &format!("rust-revocation-{index}.bin"), signature);
    }
}

// Match the callback in biscuit-auth 6.0.0 examples/testcases.rs's ffi case.
fn official_ffi_functions() -> HashMap<String, ExternFunc> {
    HashMap::from([(
        "test".to_string(),
        ExternFunc::new(Arc::new(|left, right| match (left, right) {
            (term, None) => Ok(term),
            (Term::Str(left), Some(Term::Str(right))) if left == right => {
                Ok(Term::Str("equal strings".to_string()))
            }
            (Term::Str(_), Some(Term::Str(_))) => Ok(Term::Str("different strings".to_string())),
            _ => Err("unsupported operands".to_string()),
        })),
    )])
}

fn main() {
    let args = env::args().collect::<Vec<_>>();
    if args[1] == "generate" {
        credential_vectors(Path::new(&args[2]));
        return;
    }
    assert!(matches!(args[1].as_str(), "check" | "check-ffi"));
    let directory = Path::new(&args[2]);
    let algorithm = if args[3] == "p256" {
        Algorithm::Secp256r1
    } else {
        Algorithm::Ed25519
    };
    let root = PublicKey::from_bytes_hex(&args[4], algorithm).unwrap();
    let token = Biscuit::from(fs::read(directory.join(&args[5])).unwrap(), root).unwrap();
    if args[1] == "check-ffi" {
        let builder = AuthorizerBuilder::new().set_extern_funcs(official_ffi_functions());
        assert_authorized_with(builder, &token, &args[6], args[7] == "allow");
    } else {
        assert_authorized(&token, &args[6], args[7] == "allow");
    }
}
