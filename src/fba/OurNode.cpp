
#include <time.h>
#include "fba/OurNode.h"
#include "main/Application.h"
#include "lib/util/Logging.h"

#define PREPARE_TIMEOUT_MS 1000


namespace stellar
{
	int gOperationToken = 0;

	
	OurNode::OurNode(Application &app) : Node(makePublicKey(app.mConfig.VALIDATION_SEED)), mApp(app)
	{
	}
	

	void OurNode::startNewRound(const stellarxdr::SlotBallot& firstBallot)
	{
		mState = stellarxdr::FBAStatementType::PREPARE;
		mPreferredBallot = firstBallot;

	}

	void OurNode::progressFBA()
	{
		QuorumSet::pointer qset = mApp.getFBAGateway().getOurQuorumSet();
		if(qset)
		{
			switch(getNodeState())
			{
			case stellarxdr::FBAStatementType::UNKNOWN:
			case stellarxdr::FBAStatementType::PREPARE:
				progressPrepare(qset);
				break;
			case stellarxdr::FBAStatementType::PREPARED:
				progressPrepared(qset);
				break;
			case stellarxdr::FBAStatementType::COMMIT:
				progressCommit(qset);
				break;
			case stellarxdr::FBAStatementType::COMMITTED:
				progressCommitted(qset);
				break;
			case stellarxdr::FBAStatementType::EXTERNALIZED:
				assert(false);
				break;
			}
		} else
		{
			CLOG(ERROR, "FBA") << "OurNode::progressFBA   We don't know our own Qset?";
		}
	}

	// we have either timed out or never sent a PREPARE
	void OurNode::sendNewPrepare(QuorumSet::pointer qset)
	{
		// switch to the highest valid ballot that has been sent by our Q
		Statement::pointer highestQPrepare = qset->getHighestStatement(stellarxdr::FBAStatementType::PREPARE, true,mApp);
		if(highestQPrepare)
		{
			if(highestQPrepare->getBallot().index == 1 && ballot::compare(mPreferredBallot.ballot,highestQPrepare->getBallot()))
			{  // our preferred is higher than the highest we have found
				sendStatement(stellarxdr::FBAStatementType::PREPARE, mPreferredBallot);
			} else sendStatement(stellarxdr::FBAStatementType::PREPARE, highestQPrepare->getSlotBallot());
			return;
		}

		// you are first!
		sendStatement(stellarxdr::FBAStatementType::PREPARE,mPreferredBallot);
	}

	void OurNode::sendStatement(stellarxdr::FBAStatementType type, const stellarxdr::SlotBallot& ballot)
	{
		QuorumSet::pointer qset = mApp.getFBAGateway().getOurQuorumSet();

		Statement::pointer statement=std::make_shared<Statement>(type,mNodeID, qset->getHash(), ballot);
        if(type == stellarxdr::FBAStatementType::PREPARE)
        {
            // TODO.2 need to add all our exceptions to the statement
        }
		
		statement->sign();
        StellarMessagePtr msg = std::make_shared<stellarxdr::StellarMessage>();
        msg->type(stellarxdr::FBA_MESSAGE);
        msg->fbaMessage()=statement->mEnvelope;

		mApp.getOverlayGateway().broadcastMessage(msg, Peer::pointer());
		mTimeSent[type] = std::chrono::system_clock::now();
		mState = type;
		// need to see how this effects everything
		progressFBA();

	}
	

	/*

	process once we send PREPARE A

	1) Is our ballot timed out?
		a) switch to the highest in our quorum set
	2) Are we vblocked?
		a) jump to the highest valid ballot in the lowest index that isn't vblocked
		b) if there isn't such a ballot, create a higher ballot with the most popular value.
	3) We have never sent a PREPARE statement
		a) <implies none are vblocked at this point>
		b) jump to highest valid ballot
	4) Do we have quorum?
		a) send PREPARED 
	5) Is there a higher ballot out there on the same index?
		a) switch to it
	*/
	void OurNode::progressPrepare(QuorumSet::pointer qset)
	{
		 
		Statement::pointer ourHighestPrepare = getHighestStatement(stellarxdr::FBAStatementType::PREPARE);

		if( (!ourHighestPrepare) ||
			(chrono::system_clock::now() > (mTimeSent[stellarxdr::FBAStatementType::PREPARE] + chrono::milliseconds(PREPARE_TIMEOUT_MS))))
		{   // we haven't prepared anything yet  3)
			// or our last msg has timedout  1)
			sendNewPrepare(qset);
			return;
		}

		vector< BallotSet > sortedBallots;
		qset->sortBallots(stellarxdr::FBAStatementType::PREPARE, sortedBallots);

		int aboveUsCount = 0;
		for(unsigned int n = 0; n < sortedBallots.size(); n++)
		{
			BallotSet& ballotSet = sortedBallots[n];

			if(ballot::compare(ballotSet.mBallot.ballot,ourHighestPrepare->getBallot()))
			{ // this  ballot is higher than ours
				aboveUsCount += ballotSet.mCount;
				if(aboveUsCount >= qset->getBlockingSize())
				{ // our ballot is vblocked
					// walk back and see what ballot we should jump to
					int targetIndex = ballotSet.mBallot.ballot.index;
					SlotBallotPtr bestChoice;
					for(int i = n; i >= 0; i--)
					{
						if(bestChoice && sortedBallots[i].mBallot.ballot.index > targetIndex)
						{
							// we have found the highest on the targetIndex
							break;
						}
						if(mApp.getTxHerderGateway().isValidBallotValue(sortedBallots[i].mBallot.ballot))
						{
							bestChoice = std::make_shared<stellarxdr::SlotBallot>(sortedBallots[i].mBallot);
						}
					}
					if(!bestChoice)
					{ // we are vblocked by invalid ballots
						// 
                        BallotPtr popularBallot = qset->getMostPopularBallot(stellarxdr::FBAStatementType::PREPARE, true,mApp);
						if(popularBallot)
						{
                            bestChoice = std::make_shared<stellarxdr::SlotBallot>(mPreferredBallot);
                            bestChoice->ballot = *popularBallot;
								
						} else
						{   // all ballots above us are invalid
							// need to send our best guess with a higher index
							if(ballot::compareValue(mPreferredBallot.ballot,sortedBallots[0].mBallot.ballot)) 
                                mPreferredBallot.ballot.index = sortedBallots[0].mBallot.ballot.index;
							else mPreferredBallot.ballot.index = sortedBallots[0].mBallot.ballot.index+1;

							sendStatement(stellarxdr::FBAStatementType::PREPARE, mPreferredBallot);
						}
					}
						
					if(bestChoice)
					{
						bestChoice->ballot.index = targetIndex;
						sendStatement(stellarxdr::FBAStatementType::PREPARE, *bestChoice);
					}

				}
            } else
            { // we have already proposed something higher
                BallotPtr ratifiedBallot = whatRatified(stellarxdr::FBAStatementType::PREPARE);
                if(ratifiedBallot)
                {
                    stellarxdr::SlotBallot slotBallot;
                    slotBallot.ledgerIndex = mPreferredBallot.ledgerIndex;
                    slotBallot.previousLedgerHash = mPreferredBallot.previousLedgerHash;
                    slotBallot.ballot = *ratifiedBallot;
                    sendStatement(stellarxdr::FBAStatementType::PREPARED, slotBallot);  // SANITY: do we need to roll back some messages if we were going down a different path
                }
            }
		}
	}

/*
	process once we send PREPARED A
	2) Are we vblocked?
		a) Jump back to prepare phase
	3) Do we have quorum?
		a) send COMMIT A

	LATER: we can factor out the commonalities between progressPrepared and progressCommit but let's wait till it is settled
*/

	void OurNode::progressPrepared(QuorumSet::pointer qset)
	{
		
		Statement::pointer ourHighestPrepared = getHighestStatement(stellarxdr::FBAStatementType::PREPARED);

		if(!ourHighestPrepared)
		{
			CLOG(ERROR, "FBA") << "OurNode::progressPrepared   ourHighestPrepared NULL?";
			return;
		}

		vector< BallotSet > sortedBallots;
		qset->sortBallots(stellarxdr::FBAStatementType::PREPARED, sortedBallots);

		int aboveUsCount = 0;
		for(unsigned int n = 0; n < sortedBallots.size(); n++)
		{
			BallotSet& ballotSet = sortedBallots[n];

			if(ballot::compare(ballotSet.mBallot.ballot,ourHighestPrepared->getBallot()))
			{ // this  ballot is higher than ours
				aboveUsCount += ballotSet.mCount;
				if(aboveUsCount >= qset->getBlockingSize())
				{ // our ballot is vblocked
					// jump back to the prepare phase
					mState = stellarxdr::FBAStatementType::PREPARE;
					progressPrepare(qset);
				}
			} else
			{ // we have already proposed something higher
                BallotPtr ratifiedBallot = whatRatified(stellarxdr::FBAStatementType::PREPARED);
                if(ratifiedBallot)
                {
                    stellarxdr::SlotBallot slotBallot;
                    slotBallot.ledgerIndex = mPreferredBallot.ledgerIndex;
                    slotBallot.previousLedgerHash = mPreferredBallot.previousLedgerHash;
                    slotBallot.ballot = *ratifiedBallot;
                    sendStatement(stellarxdr::FBAStatementType::COMMIT, slotBallot);  // SANITY: do we need to roll back some messages if we were going down a different path
                }
			}
		}
		
	}


	/*
	process once we send COMMIT A
	1) Are we vblocked?
		a) Jump back to prepare phase
	3) Do we have quorum?
		a) send COMMITED A
	*/
	
	void OurNode::progressCommit(QuorumSet::pointer qset)
	{
		
		Statement::pointer ourHighestCommit = getHighestStatement(stellarxdr::FBAStatementType::COMMIT);

		if(!ourHighestCommit)
		{
			CLOG(ERROR, "FBA") << "OurNode::progressCommit   ourHighestCommit NULL?";
			return;
		}

		vector< BallotSet > sortedBallots;
		qset->sortBallots(stellarxdr::FBAStatementType::PREPARED, sortedBallots);

		int aboveUsCount = 0;
		for(unsigned int n = 0; n < sortedBallots.size(); n++)
		{
			BallotSet& ballotSet = sortedBallots[n];

			if(ballot::compare(ballotSet.mBallot.ballot, ourHighestCommit->getBallot()))
			{ // this  ballot is higher than ours
				aboveUsCount += ballotSet.mCount;
				if(aboveUsCount >= qset->getBlockingSize())
				{ // our ballot is vblocked
					// jump back to the prepare phase
					mState = stellarxdr::FBAStatementType::PREPARE;
					progressPrepare(qset);
				}
			} else
			{ // we have already proposed something higher
				// see if you have a quorum that has ratified anything
				BallotPtr ratifiedBallot = whatRatified(stellarxdr::FBAStatementType::COMMIT);
                if(ratifiedBallot)
                {
                    stellarxdr::SlotBallot slotBallot;
                    slotBallot.ledgerIndex = mPreferredBallot.ledgerIndex;
                    slotBallot.previousLedgerHash = mPreferredBallot.previousLedgerHash;
                    slotBallot.ballot = *ratifiedBallot;
                    sendStatement(stellarxdr::FBAStatementType::COMMITTED, slotBallot);  // SANITY: do we need to roll back some messages if we were going down a different path
                }
			}
		}
		
	}

	/*
		process once we send COMMITTED A
		1) Do we have quorum?
		a) externalize A
	*/ 
	void OurNode::progressCommitted(QuorumSet::pointer qset)
	{	
        BallotPtr ratifiedBallot = whatRatified(stellarxdr::FBAStatementType::COMMITTED);
		if(ratifiedBallot)
		{
			Statement::pointer ourHighestCommitted = getHighestStatement(stellarxdr::FBAStatementType::COMMITTED);

			if(ourHighestCommitted)
			{
				if(ballot::isCompatible(ourHighestCommitted->getBallot(),*ratifiedBallot)) 
                    mApp.getTxHerderGateway().externalizeValue(ourHighestCommitted->getSlotBallot());
			} else
			{ // this is a non-validating node. Just check if our qset has ratified anything
                stellarxdr::SlotBallot slotBallot;
                slotBallot.ledgerIndex = mPreferredBallot.ledgerIndex;
                slotBallot.previousLedgerHash = mPreferredBallot.previousLedgerHash;
                slotBallot.ballot = *ratifiedBallot;
				mApp.getTxHerderGateway().externalizeValue(slotBallot);
			}
		}
	}
    
   

    BallotPtr OurNode::whatRatified(stellarxdr::FBAStatementType type)
    {
        QuorumSet::pointer qset = mApp.getFBAGateway().getOurQuorumSet();

        BallotPtr popularBallot = qset->getMostPopularBallot(type,true,mApp);

        int operationToken = gOperationToken++;
        int recheckIndex = 0;

        int yesVotes = 0;
        for(unsigned int n = 0; n < qset->mNodes.size(); n++)
        {
            Node::RatState state = qset->mNodes[n]->checkRatState(type, popularBallot, 
                operationToken, recheckIndex, mApp);
            if(state == PLEDGING_STATE || state == RATIFIED_STATE)
            {
                yesVotes++;
                if(yesVotes > qset->mThreshold)
                {
                    return popularBallot;
                }
            } else if(state == RECHECK_STATE)
            {
                n = 0;
                recheckIndex++;
                yesVotes = 0;
            }
        }

        return BallotPtr();
    }
}
