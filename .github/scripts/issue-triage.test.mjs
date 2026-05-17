import test from 'node:test';
import assert from 'node:assert/strict';

import {
  canSafelyCloseDuplicate,
  detectDuplicateCandidates,
  finalizeTriageDecision,
  renderComment,
  scoreDuplicateCandidate,
  selectBestGitHubModel,
} from './issue-triage.mjs';

const config = {
  duplicateHeuristics: {
    minimumCandidateScore: 0.18,
    humanReviewConfidence: 0.45,
    closeConfidence: 0.88,
    minimumFullDuplicateScore: 0.74,
    weights: {
      title: 0.34,
      body: 0.24,
      signals: 0.2,
      components: 0.12,
      phrases: 0.1,
    },
  },
  responseStyle: {
    maxPoints: 8,
    maxPlanSteps: 3,
    maxRelatedIssues: 4,
  },
  commentMarker: '<!-- openq4-issue-triage -->',
  defaultLabelsOnModelFailure: ['needs-human-review'],
  typeLabelMappings: {
    bug: 'bug',
    'feature request': 'enhancement',
    enhancement: 'enhancement',
    question: 'question',
    'support request': 'question',
    documentation: 'documentation',
    performance: 'performance',
    security: 'security',
    regression: 'regression',
    compatibility: 'compatibility',
    'build/install': 'build/install',
  },
  managedLabels: {
    'needs-info': { color: 'd876e3', description: '' },
    'needs-human-review': { color: 'fbca04', description: '' },
    duplicate: { color: 'cfd3d7', description: '' },
    question: { color: 'd4c5f9', description: '' },
    documentation: { color: '0075ca', description: '' },
    security: { color: 'b60205', description: '' },
    'build/install': { color: '5319e7', description: '' },
    compatibility: { color: '1d76db', description: '' },
    performance: { color: 'f9d0c4', description: '' },
    regression: { color: 'e99695', description: '' },
  },
};

const existingLabelsByName = new Map([
  ['bug', { name: 'bug' }],
  ['enhancement', { name: 'enhancement' }],
  ['help wanted', { name: 'help wanted' }],
]);

const currentIssue = {
  number: 100,
  title: 'Fog not working in storage2',
  body: 'The fog volumes on `game/storage2` are missing and the level looks wrong. Please ensure we support retail fog volumes again.',
  labels: [],
};

const candidateIssue = {
  number: 48,
  title: 'Fog not working',
  body: 'The fog volumes on `game/storage2` are missing - ensure we support retail Q4\'s fog volumes.',
  labels: [{ name: 'bug' }],
  html_url: 'https://github.com/themuffinator/OpenQ4/issues/48',
};

test('selectBestGitHubModel prefers the strongest GPT model', () => {
  const model = selectBestGitHubModel([
    { id: 'openai/gpt-5-mini' },
    { id: 'openai/gpt-4.1' },
    { id: 'openai/gpt-5' },
  ]);
  assert.equal(model, 'openai/gpt-5');
});

test('scoreDuplicateCandidate strongly matches nearly identical issues', () => {
  const scored = scoreDuplicateCandidate(currentIssue, candidateIssue, config.duplicateHeuristics);
  assert.ok(scored.score > 0.74, `expected strong duplicate score, received ${scored.score}`);
  assert.ok(scored.sharedSignals.includes('game/storage2'));
});

test('detectDuplicateCandidates ranks the strongest match first', () => {
  const candidates = detectDuplicateCandidates(currentIssue, [candidateIssue], config.duplicateHeuristics, 5);
  assert.equal(candidates[0].number, 48);
});

test('canSafelyCloseDuplicate requires strong ai and heuristic agreement', () => {
  const heuristicCandidates = detectDuplicateCandidates(currentIssue, [candidateIssue], config.duplicateHeuristics, 5);
  const triage = {
    duplicate: { status: 'full', confidence: 0.93 },
    relatedIssues: [
      {
        number: 48,
        relation: 'full-duplicate',
        confidence: 0.94,
        reason: 'Same missing fog volumes on the same map.',
        coveredPoints: ['Missing fog on game/storage2'],
        newPoints: [],
      },
    ],
  };
  assert.equal(canSafelyCloseDuplicate({ triage, heuristicCandidates, config }), true);
});

test('finalizeTriageDecision adds needs-info and keeps uncertain duplicates open', () => {
  const heuristicCandidates = detectDuplicateCandidates(currentIssue, [candidateIssue], config.duplicateHeuristics, 5);
  const triage = {
    summary: 'Fog appears to be missing on the storage2 map.',
    detectedPoints: ['Fog volumes appear absent on game/storage2.'],
    issueType: 'bug',
    affectedComponents: ['renderer', 'fog'],
    severity: 'medium',
    actionable: true,
    needsMoreInformation: true,
    requiresHumanReview: false,
    multipleUnrelatedRequests: false,
    missingInformation: ['A log excerpt from `.home/baseoq4/logs/openq4.log`.'],
    directResponse: 'The repository has an open fog issue, but the exact regression still needs confirmation from a log.',
    suggestedPlan: ['Confirm whether this reproduces on current builds.'],
    duplicate: { status: 'partial', confidence: 0.55 },
    relatedIssues: [
      {
        number: 48,
        relation: 'partial-overlap',
        confidence: 0.6,
        reason: 'Likely the same fog system, but the new report still needs runtime confirmation.',
        coveredPoints: ['Fog volumes missing on storage2'],
        newPoints: ['Current build validation is still missing'],
      },
    ],
    labelSuggestions: [],
    decisionSummary: 'Fog issue needs more information.',
  };

  const decision = finalizeTriageDecision({
    issue: currentIssue,
    triage,
    heuristicCandidates,
    config,
    existingLabelsByName,
  });

  assert.ok(decision.selectedLabels.includes('bug'));
  assert.ok(decision.selectedLabels.includes('needs-info'));
  assert.ok(decision.selectedLabels.includes('needs-human-review'));
  assert.equal(decision.closeIssue, false);
  assert.equal(decision.statusKey, 'needs-info');
});

test('renderComment keeps the required sections and marker', () => {
  const body = renderComment(
    {
      summary: 'Fog appears to be missing on the storage2 map.',
      detectedPoints: ['Fog volumes appear absent on game/storage2.'],
      labelReasons: [
        { name: 'bug', reason: 'Detected bug issue type.' },
        { name: 'needs-info', reason: 'Critical reproduction or environment details are still missing.' },
      ],
      responseText: 'The repository has an open fog issue, but the exact regression still needs confirmation from a log.',
      missingInformation: ['A log excerpt from `.home/baseoq4/logs/openq4.log`.'],
      relatedIssues: [],
      suggestedPlan: ['Confirm whether this reproduces on current builds.'],
      statusKey: 'needs-info',
    },
    config,
  );

  assert.match(body, /automated triage response/i);
  assert.match(body, /## Summary/);
  assert.match(body, /## Status/);
  assert.match(body, /<!-- openq4-issue-triage -->/);
});
