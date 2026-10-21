# Composition experiments: reusable generator progress

## User direction

An existing work is the listening and regression case for an experiment, not
the final scope of the improvement. Experiments must improve the general
composition process, rather than only repair the example's exported MIDI.

## Working method

1. Preserve the source MIDI, its actual generation inputs and the generator
   version as A. Distinguish a shared production profile from a local experimental
   worker and from an externally edited audition artifact.
2. Diagnose the actual notes and their ensemble context. A disliked example is
   evidence, not a universal prohibition on syncopation, chromaticism, repetition,
   counterpoint or short notes. A valid MIDI is not proof of musical quality.
3. Implement the proposed general behavior in an isolated generator workflow:
   AI musical planning, realization, contextual review or another explicit
   architectural change. All musical notes and revisions remain AI decisions.
   Do not encode the example's title, instrument names, key, bar numbers, exact
   replacement pitches or timestamps as a general composing rule.
4. Compare A/B on the source work through that experimental workflow. Preserve
   enough input/context to make the comparison interpretable. A shared seed alone
   does not guarantee identical API output. State what was actually held fixed.
5. Generate additional works from scratch using the same experimental generator,
   without the source work's repair instructions. Check whether the benefit
   transfers, including whether wanted complexity and expression were lost.
6. Present selected candidates with precise listening locations and remaining
   uncertainty. Symbolic analysis cannot establish audible preference by itself.
7. Only after generalization and user acceptance should the change be promoted
   to the shared local/Cloud profile, committed or deployed when requested.

## Reporting and boundaries

- Clearly label **generator change**, **local experimental workflow**, and
  **single-work MIDI revision**. Never imply that improving one artifact improves
  future generations automatically.
- Single-work revisions may diagnose a hypothesis or supply an auditory target,
  but they are not the endpoint and do not count as generalized progress.
- No automatic quantization, note insertion, deletion, retuning, thinning or
  random humanization as a substitute for AI musical authorship.
- Avoid endless additions of vague style instructions. Inspect whether the
  actual generated relationships carry the intended pulse, phrase and argument.
- Preserve accepted anchors, unrelated user files and ongoing jobs. Experiments
  do not automatically authorize production deployment, VST builds or paid runs
  beyond the experiment's scope.

## Current research handoff

The recent C/D/E edits of *The Fourth That Returns* are single-work audition
artifacts, not promoted generator improvements. They remain useful evidence.
The next general hypothesis concerns phrase-level rhythmic coordination:
entrance, metrical supports, articulation, continuation and responses must form
an intelligible ensemble argument, not merely individually valid note streams.
The listener identified bars 17.3–19 of the clarinet/bassoon entrance as a concrete
case, while explicitly liking the complexity at bar 53. Both observations must
inform the experiment; neither implies a hardcoded bar-specific production fix.
