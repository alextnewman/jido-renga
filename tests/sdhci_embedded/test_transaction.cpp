// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Transaction.h"

using namespace jr::sdhci;


JR_TEST(transaction, refcount_lifecycle)
{
	Transaction* t = Transaction::New(Cmd::GoIdleState);
	JR_CHECK(t != nullptr);
	JR_CHECK_EQ(t->RefCount(), 1);

	t->Retain();							// worker takes a reference
	JR_CHECK_EQ(t->RefCount(), 2);
	t->Release();							// worker done
	JR_CHECK_EQ(t->RefCount(), 1);

	t->Release();							// caller done -> self-destruct
	// (t is now dangling; nothing more to check without UAF)
}


JR_TEST(transaction, ticket_holder_releases_on_scope_exit)
{
	Transaction* t = Transaction::New(Cmd::SendCsd);
	t->Retain();							// simulate a worker reference (rc=2)
	{
		TicketHolder holder(t);
		JR_CHECK_EQ(holder->RefCount(), 2);
		JR_CHECK(holder->command == Cmd::SendCsd);
	}										// holder dtor -> Release (rc=1)
	JR_CHECK_EQ(t->RefCount(), 1);
	t->Release();							// final
}


JR_TEST(transaction, mark_done_publishes_result)
{
	Transaction* t = Transaction::New(Cmd::AllSendCid);
	JR_CHECK(!t->IsDone());
	t->MarkDone(0);
	JR_CHECK(t->IsDone());
	JR_CHECK_EQ(t->result, 0);
	t->Release();
}


JR_TEST(mailbox, post_claim_protocol)
{
	Transaction* t = Transaction::New(Cmd::GoIdleState);
	TransactionMailbox mb;

	JR_CHECK(mb.Empty());
	JR_CHECK(mb.Post(t));					// caller posts
	JR_CHECK(!mb.Empty());
	JR_CHECK(!mb.Post(t));					// invariant: slot already full

	Transaction* claimed = mb.Claim();		// worker claims
	JR_CHECK(claimed == t);
	JR_CHECK(mb.Empty());
	JR_CHECK(mb.Claim() == nullptr);		// spurious wake -> nothing

	t->Release();
}


JR_TEST(mailbox, reclaim_on_timeout)
{
	Transaction* t = Transaction::New(Cmd::GoIdleState);
	TransactionMailbox mb;

	// Timeout before the worker claimed: we pull our ticket back.
	JR_CHECK(mb.Post(t));
	JR_CHECK(mb.Reclaim(t));
	JR_CHECK(mb.Empty());

	// Worker already claimed: reclaim must fail (worker owns it now).
	JR_CHECK(mb.Post(t));
	JR_CHECK(mb.Claim() == t);
	JR_CHECK(!mb.Reclaim(t));

	t->Release();
}


JR_TEST(vcstate, readiness_gates)
{
	VirtualControllerState vc;
	// Constructed inhibited.
	JR_CHECK(!vc.ReadyForCommand());
	JR_CHECK(!vc.ReadyForData());

	vc.Update(/*cmd*/ false, /*data*/ false, /*inserted*/ true, /*reg*/ true);
	JR_CHECK(vc.ReadyForCommand());
	JR_CHECK(vc.ReadyForData());
	JR_CHECK(vc.cardInserted);

	vc.Update(false, true, true, true);		// data line busy
	JR_CHECK(vc.ReadyForCommand());
	JR_CHECK(!vc.ReadyForData());
}
