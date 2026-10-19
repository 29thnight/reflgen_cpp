//! Independent consumer-side oracle for reflgen.prsm.call.v1.
//! This is a contract test, not integration into the PrSM compiler.

#[cfg(test)]
mod tests {
    use sha2::{Digest, Sha256};

    fn encode(fields: &[&str]) -> String {
        fields
            .iter()
            .map(|s| format!("{}:{};", s.len(), s))
            .collect()
    }

    #[test]
    fn production_call_vectors() {
        let fixture = include_str!("../../fixtures/prsm-call-v1.tsv");
        let mut count = 0;
        for line in fixture
            .lines()
            .filter(|s| !s.is_empty() && !s.starts_with('#'))
        {
            let f: Vec<_> = line.split('\t').collect();
            assert_eq!(f.len(), 13);
            let params: Vec<_> = if f[10].is_empty() {
                vec![]
            } else {
                f[10].split(',').collect()
            };
            let param_count = params.len().to_string();
            let mut fields = vec![
                "reflgen.prsm.call.v1",
                "cdecl",
                "int32",
                "0",
                "typed_out_pointer",
                "no_out_pointer",
                "generational_u64",
                "domain_generation_nominal_type",
                "borrowed_call_guard",
                "game_thread",
                "caught_to_status",
                "u8_nonzero_in_normalized_out",
                "untouched",
                "synchronous",
                f[2],
                f[1],
                "64",
                f[3],
                f[4],
                f[5],
                f[6],
                f[7],
                f[8],
                f[9],
                &param_count,
            ];
            fields.extend(params);
            let descriptor = encode(&fields);
            assert_eq!(descriptor, f[11], "{} descriptor", f[0]);
            assert_eq!(
                format!("{:x}", Sha256::digest(descriptor.as_bytes())),
                f[12],
                "{} digest",
                f[0]
            );
            // A substituted symbol or return width must never validate as the old call.
            fields[17] = "ce_prsm_bind_substituted";
            assert_ne!(
                format!("{:x}", Sha256::digest(encode(&fields).as_bytes())),
                f[12]
            );
            count += 1;
        }
        assert_eq!(count, 7);
    }

    #[test]
    fn byte_lengths_and_delimiters_are_unambiguous() {
        assert_eq!(encode(&["実数", "", "a:b;c"]), "6:実数;0:;5:a:b;c;");
        assert_ne!(encode(&["ab", "c"]), encode(&["a", "bc"]));
    }
}
