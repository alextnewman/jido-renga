// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <new>
#include <stdint.h>
#include <stddef.h>

#include "Adma2.h"
#include "Command.h"

// The unit of work handed from a caller to the worker thread: a ref-counted
// ticket, the lock-free mailbox that carries it, and the cached controller
// state. Uses only compiler atomic builtins -- no kernel headers -- so the
// ownership protocol can be exercised off-target.

namespace jr::sdhci {


// Ref-counted command ticket. Created via New() with refcount 1 (held by the
// caller's TicketHolder). The worker retains it while processing and releases
// when done; the ticket self-destructs when both references drop. Result and
// response words live *in the ticket*, not in caller storage, so a worker that
// finishes after the caller has already timed out can never corrupt a caller
// stack frame; the caller copies them out only once it observes IsDone().
class Transaction {
public:
	enum class State : int8_t { Idle = 0, Done = 1 };

	// Immutable request fields, stamped by the caller before Post().
	Cmd			command;
	uint32_t	argument = 0;
	ReplyType	replyType = ReplyType::None;
	uint16_t	transferMode = 0;

	// Per-command policy and dialect are immutable after publication, preventing
	// a late worker from observing state prepared for a newer transaction.
	CommandConstraints	constraints;
	CardDialect			dialect = CardDialect::Unknown;

	// Data phase.
	bool				dataPresent = false;
	bool				tuning = false;
	uint64_t			dmaAddress = 0;		// SDMA physical address
	uint16_t			blockSize = 0;
	uint32_t			blockCount = 0;
	Adma2Descriptor*	adma2Table = nullptr;	// non-null selects ADMA2
	uint64_t			adma2Address = 0;
	uint32_t			adma2Entries = 0;

	// Result, published by the worker into the ticket itself (never a pointer
	// into caller storage). The caller copies these out only after IsDone().
	uint32_t	responseWords[4] = {0, 0, 0, 0};
	int32_t		result = 0;				// status_t at the kernel boundary
	uint32_t	epoch = 0;				// unique id, for timeout disambiguation

	static Transaction*
	New(Cmd command) noexcept
	{
		Transaction* t = new(std::nothrow) Transaction(command);
		return t;
	}

	void Retain() noexcept { __sync_fetch_and_add(&fRefCount, 1); }

	void
	Release() noexcept
	{
		if (__sync_sub_and_fetch(&fRefCount, 1) == 0)
			delete this;
	}

	bool IsDone() const noexcept
	{
		__sync_synchronize();
		return fState == State::Done;
	}

	void
	MarkDone(int32_t txResult) noexcept
	{
		result = txResult;
		__sync_synchronize();
		fState = State::Done;
	}

	int32_t RefCount() const noexcept { return fRefCount; }

private:
	explicit Transaction(Cmd cmd) noexcept : command(cmd) {}

	volatile int32_t	fRefCount = 1;
	volatile State		fState = State::Idle;
};


// Move-only RAII owner of a caller-side ticket. Releases on scope exit, so no
// command path needs a cleanup ladder.
class TicketHolder {
public:
	explicit TicketHolder(Transaction* ticket) noexcept : fTicket(ticket) {}
	~TicketHolder() { if (fTicket != nullptr) fTicket->Release(); }

	TicketHolder(const TicketHolder&) = delete;
	TicketHolder& operator=(const TicketHolder&) = delete;

	TicketHolder(TicketHolder&& other) noexcept : fTicket(other.fTicket)
	{
		other.fTicket = nullptr;
	}

	Transaction* Get() const noexcept { return fTicket; }
	Transaction* operator->() const noexcept { return fTicket; }
	explicit operator bool() const noexcept { return fTicket != nullptr; }

private:
	Transaction*	fTicket;
};


// Lock-free single-slot mailbox. The bus lock guarantees at most one poster at
// a time, so Post() always finds the slot empty; the worker is the sole
// consumer. Modeled as a class so the CAS protocol is explicit and testable.
class TransactionMailbox {
public:
	// Caller side: publish a ticket. Returns false only if the invariant is
	// violated (slot not empty) -- a programming error, never expected.
	bool
	Post(Transaction* ticket) noexcept
	{
		return __sync_bool_compare_and_swap(&fSlot, nullptr, ticket);
	}

	// Worker side: atomically take whatever is queued, leaving the slot empty.
	// Returns nullptr on a spurious wake (nothing queued).
	Transaction*
	Claim() noexcept
	{
		return __sync_lock_test_and_set(&fSlot, nullptr);
	}

	// Reclaim the ticket after timeout only if the worker has not claimed it.
	bool
	Reclaim(Transaction* ticket) noexcept
	{
		return __sync_bool_compare_and_swap(&fSlot, ticket, nullptr);
	}

	bool Empty() const noexcept
	{
		__sync_synchronize();
		return fSlot == nullptr;
	}

private:
	Transaction* volatile	fSlot = nullptr;
};


// Cached view of controller present-state, maintained by the worker (the sole
// semantic/mutating hardware owner). Callers read it to skip hardware pokes for
// common pre-checks. Pure data: the engine feeds it via Update().
struct VirtualControllerState {
	// Mutable present-state, refreshed after each attempt.
	bool	commandInhibit = true;
	bool	dataInhibit = true;
	bool	cardInserted = false;
	bool	regulatorStable = false;

	// Sticky configuration.
	CardType	cardType = CardType::Unknown;
	uint8_t		baseClockMHz = 0;
	int			dataWidthBits = 1;
	uint32_t	currentClockKHz = 400;

	void
	Update(bool cmdInhibit, bool datInhibit, bool inserted,
		bool regulator) noexcept
	{
		commandInhibit = cmdInhibit;
		dataInhibit = datInhibit;
		cardInserted = inserted;
		regulatorStable = regulator;
	}

	// True when the controller can accept a new command right now.
	bool ReadyForCommand() const noexcept { return !commandInhibit; }
	bool ReadyForData() const noexcept { return !commandInhibit && !dataInhibit; }
};


} // namespace jr::sdhci
