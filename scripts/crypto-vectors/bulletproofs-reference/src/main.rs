use bulletproofs::{BulletproofGens, PedersenGens, RangeProof};
use curve25519_dalek::ristretto::CompressedRistretto;
use curve25519_dalek::scalar::Scalar;
use merlin::Transcript;
use rand_core::{CryptoRng, Error, RngCore};
use sha3::digest::{ExtendableOutput, Update, XofReader};
use sha3::{Shake256, Shake256Reader};
use std::fmt::Write;

fn hex(bytes: &[u8]) -> String {
    let mut result = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        write!(&mut result, "{byte:02x}").unwrap();
    }
    result
}

fn merlin_vectors() {
    fn challenge(
        transcript: &mut Transcript,
        scenario: &str,
        opcode: &str,
        label: &'static [u8],
        length: usize,
    ) {
        let mut out = vec![0; length];
        transcript.challenge_bytes(label, &mut out);
        println!("{scenario}\t{opcode}\t{}\t{length}\t{}", hex(label), hex(&out));
    }

    fn append(
        transcript: &mut Transcript,
        scenario: &str,
        label: &'static [u8],
        message: &[u8],
    ) {
        transcript.append_message(label, message);
        println!("{scenario}\tappend\t{}\t{}\t", hex(label), hex(message));
    }

    let mut transcript = Transcript::new(b"neura merlin");
    println!("sequenced\tnew\t{}\t\t", hex(b"neura merlin"));
    append(&mut transcript, "sequenced", b"note", b"");
    challenge(&mut transcript, "sequenced", "challenge", b"zero", 0);
    append(&mut transcript, "sequenced", b"note", b"ordinary message");
    let number = 0xfedcba9876543210;
    transcript.append_u64(b"counter", number);
    println!("sequenced\tu64\t{}\t{number}\t", hex(b"counter"));
    challenge(&mut transcript, "sequenced", "challenge", b"output", 32);
    challenge(&mut transcript, "sequenced", "challenge", b"output", 333);
    append(&mut transcript, "sequenced", b"late", &[0xff, 0, 0x81]);
    challenge(&mut transcript, "sequenced", "fork", b"branch", 64);
    challenge(&mut transcript, "sequenced", "challenge", b"after branch", 8);

    let mut transcript = Transcript::new(b"");
    println!("boundary\tnew\t\t\t");
    // Exercise the 166-byte rate with adjacent short and long frames.
    for size in [165, 166, 167, 1024] {
        let message: Vec<u8> = (0..size).map(|i| (31 * i + 3) as u8).collect();
        append(&mut transcript, "boundary", b"cross boundary", &message);
        challenge(&mut transcript, "boundary", "challenge", b"different", size);
    }
}

struct Recorder {
    stream: Shake256Reader,
    consumed: Vec<u8>,
}

impl Recorder {
    fn new(seed: &[u8]) -> Self {
        let mut shake = Shake256::default();
        shake.update(b"Neura reference test only");
        shake.update(seed);
        Self {
            stream: shake.finalize_xof(),
            consumed: Vec::new(),
        }
    }
}

impl CryptoRng for Recorder {}

impl RngCore for Recorder {
    fn next_u32(&mut self) -> u32 {
        let mut bytes = [0; 4];
        self.fill_bytes(&mut bytes);
        u32::from_le_bytes(bytes)
    }

    fn next_u64(&mut self) -> u64 {
        let mut bytes = [0; 8];
        self.fill_bytes(&mut bytes);
        u64::from_le_bytes(bytes)
    }

    fn fill_bytes(&mut self, dest: &mut [u8]) {
        self.stream.read(dest);
        self.consumed.extend_from_slice(dest);
    }

    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), Error> {
        self.fill_bytes(dest);
        Ok(())
    }
}

fn range_vectors() {
    let pc = PedersenGens::default();
    let bp = BulletproofGens::new(64, 1);
    let label = b"neura range test";
    for (index, (bits, value)) in [(8, 0), (8, 255), (16, 65535), (32, u32::MAX as u64), (64, u64::MAX)]
        .into_iter()
        .enumerate()
    {
        let blind = if index == 0 { Scalar::ZERO } else { Scalar::from(491 + index as u64) };
        let mut random = Recorder::new(&[index as u8]);
        let (proof, commitment) = RangeProof::prove_single_with_rng(
            &bp, &pc, &mut Transcript::new(label), value, &blind, bits, &mut random,
        )
        .unwrap();
        assert_eq!(random.consumed.len(), 64 * (2 * bits + 4));
        let mut verifier_random = Recorder::new(b"verification entropy");
        proof.verify_single_with_rng(
            &bp, &pc, &mut Transcript::new(label), &commitment, bits, &mut verifier_random,
        ).unwrap();
        let bytes = proof.to_bytes();
        let wrong_commitment = pc.commit(Scalar::from(value), blind + Scalar::ONE).compress();
        assert!(proof.verify_single_with_rng(
            &bp, &pc, &mut Transcript::new(label), &wrong_commitment, bits, &mut verifier_random,
        ).is_err());
        assert!(proof.verify_single_with_rng(
            &bp, &pc, &mut Transcript::new(b"wrong transcript"), &commitment, bits, &mut verifier_random,
        ).is_err());

        // An altered scalar remains canonical, so verification exercises the equations.
        let mut altered = bytes.clone();
        let t_x: [u8; 32] = altered[128..160].try_into().unwrap();
        let modified = Scalar::from_canonical_bytes(t_x).unwrap() + Scalar::ONE;
        altered[128..160].copy_from_slice(modified.as_bytes());
        let altered_proof = RangeProof::from_bytes(&altered).unwrap();
        assert!(altered_proof.verify_single_with_rng(
            &bp, &pc, &mut Transcript::new(label), &commitment, bits, &mut verifier_random,
        ).is_err());
        let mut altered_inner = bytes.clone();
        let a_offset = altered_inner.len() - 64;
        let a: [u8; 32] = altered_inner[a_offset..a_offset + 32].try_into().unwrap();
        let modified = Scalar::from_canonical_bytes(a).unwrap() + Scalar::ONE;
        altered_inner[a_offset..a_offset + 32].copy_from_slice(modified.as_bytes());
        let altered_inner_proof = RangeProof::from_bytes(&altered_inner).unwrap();
        assert!(altered_inner_proof.verify_single_with_rng(
            &bp, &pc, &mut Transcript::new(label), &commitment, bits, &mut verifier_random,
        ).is_err());
        assert_eq!(CompressedRistretto::from_slice(commitment.as_bytes()).unwrap(), commitment);

        println!(
            "{bits}\t{value}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
            hex(blind.as_bytes()),
            hex(label),
            hex(&random.consumed),
            hex(commitment.as_bytes()),
            hex(&bytes),
            hex(wrong_commitment.as_bytes()),
            hex(&altered),
            hex(&altered_inner),
        );
    }
}

fn aggregate_vectors() {
    let pc = PedersenGens::default();
    let bp = BulletproofGens::new(64, 4);
    let label = b"neura aggregate range test";
    for (bits, values) in [(8, vec![0, 255]), (16, vec![9, 0, 32768, 65535])] {
        let blinds: Vec<_> = (0..values.len())
            .map(|index| {
                if index == 0 && bits == 8 {
                    Scalar::ZERO
                } else {
                    Scalar::from(101 + bits as u64 + 13 * index as u64)
                }
            })
            .collect();
        let mut random = Recorder::new(&[bits as u8, values.len() as u8]);
        let (proof, commitments) = RangeProof::prove_multiple_with_rng(
            &bp,
            &pc,
            &mut Transcript::new(label),
            &values,
            &blinds,
            bits,
            &mut random,
        )
        .unwrap();
        assert_eq!(random.consumed.len(), 64 * values.len() * (2 * bits + 4));
        let mut verifier_random = Recorder::new(b"aggregate verification entropy");
        let verify = |proof: &RangeProof,
                      items: &[CompressedRistretto],
                      label,
                      rng: &mut Recorder| {
            proof.verify_multiple_with_rng(&bp, &pc, &mut Transcript::new(label), items, bits, rng)
        };
        verify(&proof, &commitments, label, &mut verifier_random).unwrap();
        let mut reordered = commitments.clone();
        reordered.swap(0, 1);
        assert!(verify(&proof, &reordered, label, &mut verifier_random).is_err());
        assert!(verify(
            &proof,
            &commitments,
            b"wrong aggregate transcript",
            &mut verifier_random
        )
        .is_err());
        let mut mismatched = commitments.clone();
        mismatched[1] = pc
            .commit(Scalar::from(values[1]), blinds[1] + Scalar::ONE)
            .compress();
        assert!(verify(&proof, &mismatched, label, &mut verifier_random).is_err());

        let bytes = proof.to_bytes();
        let mut altered = bytes.clone();
        let a_offset = altered.len() - 64;
        let a: [u8; 32] = altered[a_offset..a_offset + 32].try_into().unwrap();
        let modified = Scalar::from_canonical_bytes(a).unwrap() + Scalar::ONE;
        altered[a_offset..a_offset + 32].copy_from_slice(modified.as_bytes());
        assert!(verify(
            &RangeProof::from_bytes(&altered).unwrap(),
            &commitments,
            label,
            &mut verifier_random
        )
        .is_err());

        println!(
            "{bits}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
            hex(label),
            values
                .iter()
                .map(u64::to_string)
                .collect::<Vec<_>>()
                .join(","),
            blinds
                .iter()
                .map(|item| hex(item.as_bytes()))
                .collect::<Vec<_>>()
                .join(","),
            hex(&random.consumed),
            commitments
                .iter()
                .map(|item| hex(item.as_bytes()))
                .collect::<Vec<_>>()
                .join(","),
            hex(&bytes),
            hex(mismatched[1].as_bytes()),
            hex(&altered),
        );
    }
}

fn main() {
    match std::env::args().nth(1).as_deref() {
        Some("merlin") => merlin_vectors(),
        Some("range") => range_vectors(),
        Some("aggregate") => aggregate_vectors(),
        _ => panic!("usage: neura-bulletproofs-reference merlin|range|aggregate"),
    }
}
